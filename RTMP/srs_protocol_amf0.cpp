/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifdef WIN32
#pragma warning(disable:4541)
#endif

#include "srs_protocol_amf0.h"

#include <utility>
#include <vector>
#include <sstream>
using namespace std;

#include "srs_kernel_log.h"
#include "srs_kernel_error.h"
#include "srs_kernel_stream.h"

using namespace _srs_internal;

// AMF0 marker
#define RTMP_AMF0_Number                     0x00
#define RTMP_AMF0_Boolean                     0x01
#define RTMP_AMF0_String                     0x02
#define RTMP_AMF0_Object                     0x03
#define RTMP_AMF0_MovieClip                 0x04 // reserved, not supported
#define RTMP_AMF0_Null                         0x05
#define RTMP_AMF0_Undefined                 0x06
#define RTMP_AMF0_Reference                 0x07
#define RTMP_AMF0_EcmaArray                 0x08
#define RTMP_AMF0_ObjectEnd                 0x09
#define RTMP_AMF0_StrictArray                 0x0A
#define RTMP_AMF0_Date                         0x0B
#define RTMP_AMF0_LongString                 0x0C
#define RTMP_AMF0_UnSupported                 0x0D
#define RTMP_AMF0_RecordSet                 0x0E // reserved, not supported
#define RTMP_AMF0_XmlDocument                 0x0F
#define RTMP_AMF0_TypedObject                 0x10
// AVM+ object is the AMF3 object.
#define RTMP_AMF0_AVMplusObject             0x11
// origin array whos data takes the same form as LengthValueBytes
#define RTMP_AMF0_OriginStrictArray         0x20

// User defined
#define RTMP_AMF0_Invalid                     0x3F

SrsAmf0Any::SrsAmf0Any()
{
    marker = RTMP_AMF0_Invalid;
}

SrsAmf0Any::~SrsAmf0Any()
{
}

bool SrsAmf0Any::is_string()
{
    return marker == RTMP_AMF0_String;
}

bool SrsAmf0Any::is_boolean()
{
    return marker == RTMP_AMF0_Boolean;
}

bool SrsAmf0Any::is_number()
{
    return marker == RTMP_AMF0_Number;
}

bool SrsAmf0Any::is_null()
{
    return marker == RTMP_AMF0_Null;
}

bool SrsAmf0Any::is_undefined()
{
    return marker == RTMP_AMF0_Undefined;
}

bool SrsAmf0Any::is_object()
{
    return marker == RTMP_AMF0_Object;
}

bool SrsAmf0Any::is_ecma_array()
{
    return marker == RTMP_AMF0_EcmaArray;
}

bool SrsAmf0Any::is_strict_array()
{
    return marker == RTMP_AMF0_StrictArray;
}

bool SrsAmf0Any::is_complex_object()
{
    return is_object() || is_object_eof() || is_ecma_array() || is_strict_array();
}

string SrsAmf0Any::to_str()
{
    SrsAmf0String* p = (SrsAmf0String* )(this);
    srs_assert(p != NULL);
    return p->value;
}

const char* SrsAmf0Any::to_str_raw()
{
    SrsAmf0String* p = (SrsAmf0String* )(this);
    srs_assert(p != NULL);
    return p->value.data();
}

bool SrsAmf0Any::to_boolean()
{
    SrsAmf0Boolean* p = (SrsAmf0Boolean* )(this);
    srs_assert(p != NULL);
    return p->value;
}

double SrsAmf0Any::to_number()
{
    SrsAmf0Number* p = (SrsAmf0Number* )(this);
    srs_assert(p != NULL);
    return p->value;
}

SrsAmf0Object* SrsAmf0Any::to_object()
{
    SrsAmf0Object* p = (SrsAmf0Object* )(this);
    srs_assert(p != NULL);
    return p;
}

SrsAmf0EcmaArray* SrsAmf0Any::to_ecma_array()
{
    SrsAmf0EcmaArray* p = (SrsAmf0EcmaArray* )(this);
    srs_assert(p != NULL);
    return p;
}

SrsAmf0StrictArray* SrsAmf0Any::to_strict_array()
{
    SrsAmf0StrictArray* p = (SrsAmf0StrictArray* )(this);
    srs_assert(p != NULL);
    return p;
}

void SrsAmf0Any::set_number(double value)
{
    SrsAmf0Number* p = (SrsAmf0Number* )(this);
    srs_assert(p != NULL);
    p->value = value;
}

bool SrsAmf0Any::is_object_eof()
{
    return marker == RTMP_AMF0_ObjectEnd;
}

void __srs_fill_level_spaces(stringstream& ss, int level)
{
    for (int i = 0; i < level; i++) {
        ss << "    ";
    }
}
void __srs_amf0_do_print(SrsAmf0Any* any, stringstream& ss, int level)
{
    if (any->is_boolean()) {
        ss << "Boolean " << (any->to_boolean()? "true":"false") << endl;
    } else if (any->is_number()) {
        ss << "Number " << std::fixed << any->to_number() << endl;
    } else if (any->is_string()) {
        ss << "String " << any->to_str() << endl;
    } else if (any->is_null()) {
        ss << "Null" << endl;
    } else if (any->is_ecma_array()) {
        SrsAmf0EcmaArray* obj = any->to_ecma_array();
        ss << "EcmaArray " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            __srs_fill_level_spaces(ss, level + 1);
            ss << "Elem '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_complex_object()) {
                __srs_amf0_do_print(obj->value_at(i), ss, level + 1);
            } else {
                __srs_amf0_do_print(obj->value_at(i), ss, 0);
            }
        }
    } else if (any->is_strict_array()) {
        SrsAmf0StrictArray* obj = any->to_strict_array();
        ss << "StrictArray " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            __srs_fill_level_spaces(ss, level + 1);
            ss << "Elem ";
            if (obj->at(i)->is_complex_object()) {
                __srs_amf0_do_print(obj->at(i), ss, level + 1);
            } else {
                __srs_amf0_do_print(obj->at(i), ss, 0);
            }
        }
    } else if (any->is_object()) {
        SrsAmf0Object* obj = any->to_object();
        ss << "Object " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            __srs_fill_level_spaces(ss, level + 1);
            ss << "Property '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_complex_object()) {
                __srs_amf0_do_print(obj->value_at(i), ss, level + 1);
            } else {
                __srs_amf0_do_print(obj->value_at(i), ss, 0);
            }
        }
    } else {
        ss << "Unknown" << endl;
    }
}

char* SrsAmf0Any::human_print(char** pdata, int* psize)
{
    stringstream ss;
    
    ss.precision(1);
    
    __srs_amf0_do_print(this, ss, 0);
    
    string str = ss.str();
    if (str.empty()) {
        return NULL;
    }
    
    char* data = new char[str.length() + 1];
    memcpy(data, str.data(), str.length());
    data[str.length()] = 0;
    
    if (pdata) {
        *pdata = data;
    }
    if (psize) {
        *psize = str.length();
    }
    
    return data;
}

SrsAmf0Any* SrsAmf0Any::str(const char* value)
{
    return new SrsAmf0String(value);
}

SrsAmf0Any* SrsAmf0Any::boolean(bool value)
{
    return new SrsAmf0Boolean(value);
}

SrsAmf0Any* SrsAmf0Any::number(double value)
{
    return new SrsAmf0Number(value);
}

SrsAmf0Any* SrsAmf0Any::null()
{
    return new SrsAmf0Null();
}

SrsAmf0Any* SrsAmf0Any::undefined()
{
    return new SrsAmf0Undefined();
}

SrsAmf0Object* SrsAmf0Any::object()
{
    return new SrsAmf0Object();
}

SrsAmf0Any* SrsAmf0Any::object_eof()
{
    return new SrsAmf0ObjectEOF();
}

SrsAmf0EcmaArray* SrsAmf0Any::ecma_array()
{
    return new SrsAmf0EcmaArray();
}

SrsAmf0StrictArray* SrsAmf0Any::strict_array()
{
    return new SrsAmf0StrictArray();
}

int SrsAmf0Any::discovery(SrsStream* stream, SrsAmf0Any** ppvalue)
{
    int ret = ERROR_SUCCESS;
    
    // detect the object-eof specially
    if (srs_amf0_is_object_eof(stream)) {
        *ppvalue = new SrsAmf0ObjectEOF();
        return ret;
    }
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read any marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    srs_verbose("amf0 any marker success");
    
    // backward the 1byte marker.
    stream->skip(-1);
    
    switch (marker) {
        case RTMP_AMF0_String: {
            *ppvalue = SrsAmf0Any::str();
            return ret;
        }
        case RTMP_AMF0_Boolean: {
            *ppvalue = SrsAmf0Any::boolean();
            return ret;
        }
        case RTMP_AMF0_Number: {
            *ppvalue = SrsAmf0Any::number();
            return ret;
        }
        case RTMP_AMF0_Null: {
            *ppvalue = SrsAmf0Any::null();
            return ret;
        }
        case RTMP_AMF0_Undefined: {
            *ppvalue = SrsAmf0Any::undefined();
            return ret;
        }
        case RTMP_AMF0_Object: {
            *ppvalue = SrsAmf0Any::object();
            return ret;
        }
        case RTMP_AMF0_EcmaArray: {
            *ppvalue = SrsAmf0Any::ecma_array();
            return ret;
        }
        case RTMP_AMF0_StrictArray: {
            *ppvalue = SrsAmf0Any::strict_array();
            return ret;
        }
        case RTMP_AMF0_Invalid:
        default: {
            ret = ERROR_RTMP_AMF0_INVALID;
            srs_error("invalid amf0 message type. marker=%#x, ret=%d", marker, ret);
            return ret;
        }
    }
    
    return ret;
}

SrsUnSortedHashtable::SrsUnSortedHashtable()
{
}

SrsUnSortedHashtable::~SrsUnSortedHashtable()
{
    clear();
}

int SrsUnSortedHashtable::count()
{
    return (int)properties.size();
}

void SrsUnSortedHashtable::clear()
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        SrsAmf0Any* any = elem.second;
        srs_freep(any);
    }
    properties.clear();
}

string SrsUnSortedHashtable::key_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.first;
}

const char* SrsUnSortedHashtable::key_raw_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.first.data();
}

SrsAmf0Any* SrsUnSortedHashtable::value_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.second;
}

void SrsUnSortedHashtable::set(string key, SrsAmf0Any* value)
{
    if (!value) {
        srs_warn("add a NULL propertity %s", key.c_str());
        return;
    }
    
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string name = elem.first;
        SrsAmf0Any* any = elem.second;
        
        if (key == name) {
            srs_freep(any);
            properties.erase(it);
            break;
        }
    }
    
    properties.push_back(std::make_pair(key, value));
}

SrsAmf0Any* SrsUnSortedHashtable::get_property(string name)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsAmf0Any* any = elem.second;
        if (key == name) {
            return any;
        }
    }
    
    return NULL;
}

SrsAmf0Any* SrsUnSortedHashtable::ensure_property_string(string name)
{
    SrsAmf0Any* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_string()) {
        return NULL;
    }
    
    return prop;
}

SrsAmf0Any* SrsUnSortedHashtable::ensure_property_number(string name)
{
    SrsAmf0Any* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_number()) {
        return NULL;
    }
    
    return prop;
}

void SrsUnSortedHashtable::copy(SrsUnSortedHashtable* src)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    for (it = src->properties.begin(); it != src->properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsAmf0Any* any = elem.second;
        set(key, any->copy());
    }
}

SrsAmf0ObjectEOF::SrsAmf0ObjectEOF()
{
    marker = RTMP_AMF0_ObjectEnd;
    utf8_empty = 0x00;
}

SrsAmf0ObjectEOF::~SrsAmf0ObjectEOF()
{
}

int SrsAmf0ObjectEOF::total_size()
{
    return SrsAmf0Size::object_eof();
}

int SrsAmf0ObjectEOF::read(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // value
    if (!stream->require(2)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read object eof value failed. ret=%d", ret);
        return ret;
    }
    int16_t temp = stream->read_2bytes();
    if (temp != 0x00) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read object eof value check failed. "
            "must be 0x00, actual is %#x, ret=%d", temp, ret);
        return ret;
    }
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read object eof marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_ObjectEnd) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check object eof marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_ObjectEnd, ret);
        return ret;
    }
    srs_verbose("amf0 read object eof marker success");
    
    srs_verbose("amf0 read object eof success");
    
    return ret;
}
int SrsAmf0ObjectEOF::write(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // value
    if (!stream->require(2)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write object eof value failed. ret=%d", ret);
        return ret;
    }
    stream->write_2bytes(0x00);
    srs_verbose("amf0 write object eof value success");
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write object eof marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_ObjectEnd);
    
    srs_verbose("amf0 read object eof success");
    
    return ret;
}

SrsAmf0Any* SrsAmf0ObjectEOF::copy()
{
    return new SrsAmf0ObjectEOF();
}

SrsAmf0Object::SrsAmf0Object()
{
    properties = new SrsUnSortedHashtable();
    eof = new SrsAmf0ObjectEOF();
    marker = RTMP_AMF0_Object;
}

SrsAmf0Object::~SrsAmf0Object()
{
    srs_freep(properties);
    srs_freep(eof);
}

int SrsAmf0Object::total_size()
{
    int size = 1;
    
    for (int i = 0; i < properties->count(); i++){
        std::string name = key_at(i);
        SrsAmf0Any* value = value_at(i);
        
        size += SrsAmf0Size::utf8(name);
        size += SrsAmf0Size::any(value);
    }
    
    size += SrsAmf0Size::object_eof();
    
    return size;
}

int SrsAmf0Object::read(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read object marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Object) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check object marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Object, ret);
        return ret;
    }
    srs_verbose("amf0 read object marker success");
    
    // value
    while (!stream->empty()) {
        // detect whether is eof.
        if (srs_amf0_is_object_eof(stream)) {
            SrsAmf0ObjectEOF pbj_eof;
            if ((ret = pbj_eof.read(stream)) != ERROR_SUCCESS) {
                srs_error("amf0 object read eof failed. ret=%d", ret);
                return ret;
            }
            srs_info("amf0 read object EOF.");
            break;
        }
        
        // property-name: utf8 string
        std::string property_name;
        if ((ret = srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
            srs_error("amf0 object read property name failed. ret=%d", ret);
            return ret;
        }
        // property-value: any
        SrsAmf0Any* property_value = NULL;
        if ((ret = srs_amf0_read_any(stream, &property_value)) != ERROR_SUCCESS) {
            srs_error("amf0 object read property_value failed. "
                "name=%s, ret=%d", property_name.c_str(), ret);
            srs_freep(property_value);
            return ret;
        }
        
        // add property
        this->set(property_name, property_value);
    }
    
    return ret;
}

int SrsAmf0Object::write(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write object marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_Object);
    srs_verbose("amf0 write object marker success");
    
    // value
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        if ((ret = srs_amf0_write_utf8(stream, name)) != ERROR_SUCCESS) {
            srs_error("write object property name failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = srs_amf0_write_any(stream, any)) != ERROR_SUCCESS) {
            srs_error("write object property value failed. ret=%d", ret);
            return ret;
        }
        
        srs_verbose("write amf0 property success. name=%s", name.c_str());
    }
    
    if ((ret = eof->write(stream)) != ERROR_SUCCESS) {
        srs_error("write object eof failed. ret=%d", ret);
        return ret;
    }
    
    srs_verbose("write amf0 object success.");
    
    return ret;
}

SrsAmf0Any* SrsAmf0Object::copy()
{
    SrsAmf0Object* copy = new SrsAmf0Object();
    copy->properties->copy(properties);
    return copy;
}

void SrsAmf0Object::clear()
{
    properties->clear();
}

int SrsAmf0Object::count()
{
    return properties->count();
}

string SrsAmf0Object::key_at(int index)
{
    return properties->key_at(index);
}

const char* SrsAmf0Object::key_raw_at(int index)
{
    return properties->key_raw_at(index);
}

SrsAmf0Any* SrsAmf0Object::value_at(int index)
{
    return properties->value_at(index);
}

void SrsAmf0Object::set(string key, SrsAmf0Any* value)
{
    properties->set(key, value);
}

SrsAmf0Any* SrsAmf0Object::get_property(string name)
{
    return properties->get_property(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_string(string name)
{
    return properties->ensure_property_string(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_number(string name)
{
    return properties->ensure_property_number(name);
}

SrsAmf0EcmaArray::SrsAmf0EcmaArray()
{
    _count = 0;
    properties = new SrsUnSortedHashtable();
    eof = new SrsAmf0ObjectEOF();
    marker = RTMP_AMF0_EcmaArray;
}

SrsAmf0EcmaArray::~SrsAmf0EcmaArray()
{
    srs_freep(properties);
    srs_freep(eof);
}

int SrsAmf0EcmaArray::total_size()
{
    int size = 1 + 4;
    
    for (int i = 0; i < properties->count(); i++){
        std::string name = key_at(i);
        SrsAmf0Any* value = value_at(i);
        
        size += SrsAmf0Size::utf8(name);
        size += SrsAmf0Size::any(value);
    }
    
    size += SrsAmf0Size::object_eof();
    
    return size;
}

int SrsAmf0EcmaArray::read(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read ecma_array marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_EcmaArray) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check ecma_array marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Object, ret);
        return ret;
    }
    srs_verbose("amf0 read ecma_array marker success");

    // count
    if (!stream->require(4)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read ecma_array count failed. ret=%d", ret);
        return ret;
    }
    
    int32_t count = stream->read_4bytes();
    srs_verbose("amf0 read ecma_array count success. count=%d", count);
    
    // value
    this->_count = count;

    while (!stream->empty()) {
        // detect whether is eof.
        if (srs_amf0_is_object_eof(stream)) {
            SrsAmf0ObjectEOF pbj_eof;
            if ((ret = pbj_eof.read(stream)) != ERROR_SUCCESS) {
                srs_error("amf0 ecma_array read eof failed. ret=%d", ret);
                return ret;
            }
            srs_info("amf0 read ecma_array EOF.");
            break;
        }
        
        // property-name: utf8 string
        std::string property_name;
        if ((ret =srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
            srs_error("amf0 ecma_array read property name failed. ret=%d", ret);
            return ret;
        }
        // property-value: any
        SrsAmf0Any* property_value = NULL;
        if ((ret = srs_amf0_read_any(stream, &property_value)) != ERROR_SUCCESS) {
            srs_error("amf0 ecma_array read property_value failed. "
                "name=%s, ret=%d", property_name.c_str(), ret);
            return ret;
        }
        
        // add property
        this->set(property_name, property_value);
    }
    
    return ret;
}
int SrsAmf0EcmaArray::write(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write ecma_array marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_EcmaArray);
    srs_verbose("amf0 write ecma_array marker success");

    // count
    if (!stream->require(4)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write ecma_array count failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(this->_count);
    srs_verbose("amf0 write ecma_array count success. count=%d", _count);
    
    // value
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        if ((ret = srs_amf0_write_utf8(stream, name)) != ERROR_SUCCESS) {
            srs_error("write ecma_array property name failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = srs_amf0_write_any(stream, any)) != ERROR_SUCCESS) {
            srs_error("write ecma_array property value failed. ret=%d", ret);
            return ret;
        }
        
        srs_verbose("write amf0 property success. name=%s", name.c_str());
    }
    
    if ((ret = eof->write(stream)) != ERROR_SUCCESS) {
        srs_error("write ecma_array eof failed. ret=%d", ret);
        return ret;
    }
    
    srs_verbose("write ecma_array object success.");
    
    return ret;
}

SrsAmf0Any* SrsAmf0EcmaArray::copy()
{
    SrsAmf0EcmaArray* copy = new SrsAmf0EcmaArray();
    copy->properties->copy(properties);
    copy->_count = _count;
    return copy;
}

void SrsAmf0EcmaArray::clear()
{
    properties->clear();
}

int SrsAmf0EcmaArray::count()
{
    return properties->count();
}

string SrsAmf0EcmaArray::key_at(int index)
{
    return properties->key_at(index);
}

const char* SrsAmf0EcmaArray::key_raw_at(int index)
{
    return properties->key_raw_at(index);
}

SrsAmf0Any* SrsAmf0EcmaArray::value_at(int index)
{
    return properties->value_at(index);
}

void SrsAmf0EcmaArray::set(string key, SrsAmf0Any* value)
{
    properties->set(key, value);
}

SrsAmf0Any* SrsAmf0EcmaArray::get_property(string name)
{
    return properties->get_property(name);
}

SrsAmf0Any* SrsAmf0EcmaArray::ensure_property_string(string name)
{
    return properties->ensure_property_string(name);
}

SrsAmf0Any* SrsAmf0EcmaArray::ensure_property_number(string name)
{
    return properties->ensure_property_number(name);
}

SrsAmf0StrictArray::SrsAmf0StrictArray()
{
    marker = RTMP_AMF0_StrictArray;
    _count = 0;
}

SrsAmf0StrictArray::~SrsAmf0StrictArray()
{
    std::vector<SrsAmf0Any*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0Any* any = *it;
        srs_freep(any);
    }
    properties.clear();
}

int SrsAmf0StrictArray::total_size()
{
    int size = 1 + 4;
    
    for (int i = 0; i < (int)properties.size(); i++){
        SrsAmf0Any* any = properties[i];
        size += any->total_size();
    }
    
    return size;
}

int SrsAmf0StrictArray::read(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read strict_array marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_StrictArray) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check strict_array marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Object, ret);
        return ret;
    }
    srs_verbose("amf0 read strict_array marker success");

    // count
    if (!stream->require(4)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read strict_array count failed. ret=%d", ret);
        return ret;
    }
    
    int32_t count = stream->read_4bytes();
    srs_verbose("amf0 read strict_array count success. count=%d", count);
    
    // value
    this->_count = count;

    for (int i = 0; i < count && !stream->empty(); i++) {
        // property-value: any
        SrsAmf0Any* elem = NULL;
        if ((ret = srs_amf0_read_any(stream, &elem)) != ERROR_SUCCESS) {
            srs_error("amf0 strict_array read value failed. ret=%d", ret);
            return ret;
        }
        
        // add property
        properties.push_back(elem);
    }
    
    return ret;
}
int SrsAmf0StrictArray::write(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write strict_array marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_StrictArray);
    srs_verbose("amf0 write strict_array marker success");

    // count
    if (!stream->require(4)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write strict_array count failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(this->_count);
    srs_verbose("amf0 write strict_array count success. count=%d", _count);
    
    // value
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsAmf0Any* any = properties[i];
        
        if ((ret = srs_amf0_write_any(stream, any)) != ERROR_SUCCESS) {
            srs_error("write strict_array property value failed. ret=%d", ret);
            return ret;
        }
        
        srs_verbose("write amf0 property success.");
    }
    
    srs_verbose("write strict_array object success.");
    
    return ret;
}

SrsAmf0Any* SrsAmf0StrictArray::copy()
{
    SrsAmf0StrictArray* copy = new SrsAmf0StrictArray();
    
    std::vector<SrsAmf0Any*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0Any* any = *it;
        copy->append(any->copy());
    }
    
    copy->_count = _count;
    return copy;
}

void SrsAmf0StrictArray::clear()
{
    properties.clear();
}

int SrsAmf0StrictArray::count()
{
    return properties.size();
}

SrsAmf0Any* SrsAmf0StrictArray::at(int index)
{
    srs_assert(index < (int)properties.size());
    return properties.at(index);
}

void SrsAmf0StrictArray::append(SrsAmf0Any* any)
{
    properties.push_back(any);
    _count = (int32_t)properties.size();
}

int SrsAmf0Size::utf8(string value)
{
    return 2 + value.length();
}

int SrsAmf0Size::str(string value)
{
    return 1 + SrsAmf0Size::utf8(value);
}

int SrsAmf0Size::number()
{
    return 1 + 8;
}

int SrsAmf0Size::null()
{
    return 1;
}

int SrsAmf0Size::undefined()
{
    return 1;
}

int SrsAmf0Size::boolean()
{
    return 1 + 1;
}

int SrsAmf0Size::object(SrsAmf0Object* obj)
{
    if (!obj) {
        return 0;
    }
    
    return obj->total_size();
}

int SrsAmf0Size::object_eof()
{
    return 2 + 1;
}

int SrsAmf0Size::ecma_array(SrsAmf0EcmaArray* arr)
{
    if (!arr) {
        return 0;
    }
    
    return arr->total_size();
}

int SrsAmf0Size::strict_array(SrsAmf0StrictArray* arr)
{
    if (!arr) {
        return 0;
    }
    
    return arr->total_size();
}

int SrsAmf0Size::any(SrsAmf0Any* o)
{
    if (!o) {
        return 0;
    }
    
    return o->total_size();
}

SrsAmf0String::SrsAmf0String(const char* _value)
{
    marker = RTMP_AMF0_String;
    if (_value) {
        value = _value;
    }
}

SrsAmf0String::~SrsAmf0String()
{
}

int SrsAmf0String::total_size()
{
    return SrsAmf0Size::str(value);
}

int SrsAmf0String::read(SrsStream* stream)
{
    return srs_amf0_read_string(stream, value);
}

int SrsAmf0String::write(SrsStream* stream)
{
    return srs_amf0_write_string(stream, value);
}

SrsAmf0Any* SrsAmf0String::copy()
{
    SrsAmf0String* copy = new SrsAmf0String(value.c_str());
    return copy;
}

SrsAmf0Boolean::SrsAmf0Boolean(bool _value)
{
    marker = RTMP_AMF0_Boolean;
    value = _value;
}

SrsAmf0Boolean::~SrsAmf0Boolean()
{
}

int SrsAmf0Boolean::total_size()
{
    return SrsAmf0Size::boolean();
}

int SrsAmf0Boolean::read(SrsStream* stream)
{
    return srs_amf0_read_boolean(stream, value);
}

int SrsAmf0Boolean::write(SrsStream* stream)
{
    return srs_amf0_write_boolean(stream, value);
}

SrsAmf0Any* SrsAmf0Boolean::copy()
{
    SrsAmf0Boolean* copy = new SrsAmf0Boolean(value);
    return copy;
}

SrsAmf0Number::SrsAmf0Number(double _value)
{
    marker = RTMP_AMF0_Number;
    value = _value;
}

SrsAmf0Number::~SrsAmf0Number()
{
}

int SrsAmf0Number::total_size()
{
    return SrsAmf0Size::number();
}

int SrsAmf0Number::read(SrsStream* stream)
{
    return srs_amf0_read_number(stream, value);
}

int SrsAmf0Number::write(SrsStream* stream)
{
    return srs_amf0_write_number(stream, value);
}

SrsAmf0Any* SrsAmf0Number::copy()
{
    SrsAmf0Number* copy = new SrsAmf0Number(value);
    return copy;
}

SrsAmf0Null::SrsAmf0Null()
{
    marker = RTMP_AMF0_Null;
}

SrsAmf0Null::~SrsAmf0Null()
{
}

int SrsAmf0Null::total_size()
{
    return SrsAmf0Size::null();
}

int SrsAmf0Null::read(SrsStream* stream)
{
    return srs_amf0_read_null(stream);
}

int SrsAmf0Null::write(SrsStream* stream)
{
    return srs_amf0_write_null(stream);
}

SrsAmf0Any* SrsAmf0Null::copy()
{
    SrsAmf0Null* copy = new SrsAmf0Null();
    return copy;
}

SrsAmf0Undefined::SrsAmf0Undefined()
{
    marker = RTMP_AMF0_Undefined;
}

SrsAmf0Undefined::~SrsAmf0Undefined()
{
}

int SrsAmf0Undefined::total_size()
{
    return SrsAmf0Size::undefined();
}

int SrsAmf0Undefined::read(SrsStream* stream)
{
    return srs_amf0_read_undefined(stream);
}

int SrsAmf0Undefined::write(SrsStream* stream)
{
    return srs_amf0_write_undefined(stream);
}

SrsAmf0Any* SrsAmf0Undefined::copy()
{
    SrsAmf0Undefined* copy = new SrsAmf0Undefined();
    return copy;
}

int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any** ppvalue)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsAmf0Any::discovery(stream, ppvalue)) != ERROR_SUCCESS) {
        srs_error("amf0 discovery any elem failed. ret=%d", ret);
        return ret;
    }
    
    srs_assert(*ppvalue);
    
    if ((ret = (*ppvalue)->read(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 parse elem failed. ret=%d", ret);
        srs_freep(*ppvalue);
        return ret;
    }
    
    return ret;
}

int srs_amf0_read_string(SrsStream* stream, string& value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read string marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_String) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check string marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_String, ret);
        return ret;
    }
    srs_verbose("amf0 read string marker success");
    
    return srs_amf0_read_utf8(stream, value);
}

int srs_amf0_write_string(SrsStream* stream, string value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write string marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_String);
    srs_verbose("amf0 write string marker success");
    
    return srs_amf0_write_utf8(stream, value);
}

int srs_amf0_read_boolean(SrsStream* stream, bool& value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read bool marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Boolean) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check bool marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Boolean, ret);
        return ret;
    }
    srs_verbose("amf0 read bool marker success");

    // value
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read bool value failed. ret=%d", ret);
        return ret;
    }

    if (stream->read_1bytes() == 0) {
        value = false;
    } else {
        value = true;
    }
    
    srs_verbose("amf0 read bool value success. value=%d", value);
    
    return ret;
}
int srs_amf0_write_boolean(SrsStream* stream, bool value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write bool marker failed. ret=%d", ret);
        return ret;
    }
    stream->write_1bytes(RTMP_AMF0_Boolean);
    srs_verbose("amf0 write bool marker success");

    // value
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write bool value failed. ret=%d", ret);
        return ret;
    }

    if (value) {
        stream->write_1bytes(0x01);
    } else {
        stream->write_1bytes(0x00);
    }
    
    srs_verbose("amf0 write bool value success. value=%d", value);
    
    return ret;
}

int srs_amf0_read_number(SrsStream* stream, double& value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read number marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Number) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check number marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Number, ret);
        return ret;
    }
    srs_verbose("amf0 read number marker success");

    // value
    if (!stream->require(8)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read number value failed. ret=%d", ret);
        return ret;
    }

    int64_t temp = stream->read_8bytes();
    memcpy(&value, &temp, 8);
    
    srs_verbose("amf0 read number value success. value=%.2f", value);
    
    return ret;
}
int srs_amf0_write_number(SrsStream* stream, double value)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write number marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_Number);
    srs_verbose("amf0 write number marker success");

    // value
    if (!stream->require(8)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write number value failed. ret=%d", ret);
        return ret;
    }

    int64_t temp = 0x00;
    memcpy(&temp, &value, 8);
    stream->write_8bytes(temp);
    
    srs_verbose("amf0 write number value success. value=%.2f", value);
    
    return ret;
}

int srs_amf0_read_null(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read null marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Null) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check null marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Null, ret);
        return ret;
    }
    srs_verbose("amf0 read null success");
    
    return ret;
}
int srs_amf0_write_null(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write null marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_Null);
    srs_verbose("amf0 write null marker success");
    
    return ret;
}

int srs_amf0_read_undefined(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read undefined marker failed. ret=%d", ret);
        return ret;
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Undefined) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 check undefined marker failed. "
            "marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Undefined, ret);
        return ret;
    }
    srs_verbose("amf0 read undefined success");
    
    return ret;
}
int srs_amf0_write_undefined(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    // marker
    if (!stream->require(1)) {
        ret = ERROR_RTMP_AMF0_ENCODE;
        srs_error("amf0 write undefined marker failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_1bytes(RTMP_AMF0_Undefined);
    srs_verbose("amf0 write undefined marker success");
    
    return ret;
}


namespace _srs_internal
{
    int srs_amf0_read_utf8(SrsStream* stream, string& value)
    {
        int ret = ERROR_SUCCESS;
        
        // len
        if (!stream->require(2)) {
            ret = ERROR_RTMP_AMF0_DECODE;
            srs_error("amf0 read string length failed. ret=%d", ret);
            return ret;
        }
        int16_t len = stream->read_2bytes();
        srs_verbose("amf0 read string length success. len=%d", len);
        
        // empty string
        if (len <= 0) {
            srs_verbose("amf0 read empty string. ret=%d", ret);
            return ret;
        }
        
        // data
        if (!stream->require(len)) {
            ret = ERROR_RTMP_AMF0_DECODE;
            srs_error("amf0 read string data failed. ret=%d", ret);
            return ret;
        }
        std::string str = stream->read_string(len);
        
        // support utf8-1 only
        // 1.3.1 Strings and UTF-8
        // UTF8-1 = %x00-7F
        // TODO: support other utf-8 strings
        /*for (int i = 0; i < len; i++) {
            char ch = *(str.data() + i);
            if ((ch & 0x80) != 0) {
                ret = ERROR_RTMP_AMF0_DECODE;
                srs_error("ignored. only support utf8-1, 0x00-0x7F, actual is %#x. ret=%d", (int)ch, ret);
                ret = ERROR_SUCCESS;
            }
        }*/
        
        value = str;
        srs_verbose("amf0 read string data success. str=%s", str.c_str());
        
        return ret;
    }
    int srs_amf0_write_utf8(SrsStream* stream, string value)
    {
        int ret = ERROR_SUCCESS;
        
        // len
        if (!stream->require(2)) {
            ret = ERROR_RTMP_AMF0_ENCODE;
            srs_error("amf0 write string length failed. ret=%d", ret);
            return ret;
        }
        stream->write_2bytes(value.length());
        srs_verbose("amf0 write string length success. len=%d", (int)value.length());
        
        // empty string
        if (value.length() <= 0) {
            srs_verbose("amf0 write empty string. ret=%d", ret);
            return ret;
        }
        
        // data
        if (!stream->require(value.length())) {
            ret = ERROR_RTMP_AMF0_ENCODE;
            srs_error("amf0 write string data failed. ret=%d", ret);
            return ret;
        }
        stream->write_string(value);
        srs_verbose("amf0 write string data success. str=%s", value.c_str());
        
        return ret;
    }
    
    bool srs_amf0_is_object_eof(SrsStream* stream) 
    {
        // detect the object-eof specially
        if (stream->require(3)) {
            int32_t flag = stream->read_3bytes();
            stream->skip(-3);
            
            return 0x09 == flag;
        }
        
        return false;
    }
    
    int srs_amf0_write_object_eof(SrsStream* stream, SrsAmf0ObjectEOF* value)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(value != NULL);
        
        // value
        if (!stream->require(2)) {
            ret = ERROR_RTMP_AMF0_ENCODE;
            srs_error("amf0 write object eof value failed. ret=%d", ret);
            return ret;
        }
        stream->write_2bytes(0x00);
        srs_verbose("amf0 write object eof value success");
        
        // marker
        if (!stream->require(1)) {
            ret = ERROR_RTMP_AMF0_ENCODE;
            srs_error("amf0 write object eof marker failed. ret=%d", ret);
            return ret;
        }
        
        stream->write_1bytes(RTMP_AMF0_ObjectEnd);
        
        srs_verbose("amf0 read object eof success");
        
        return ret;
    }

    int srs_amf0_write_any(SrsStream* stream, SrsAmf0Any* value)
    {
        srs_assert(value != NULL);
        return value->write(stream);
    }
}

