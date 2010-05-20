#include <v8.h>
#include <node_object_wrap.h>
#include <node.h>
#include <python2.6/Python.h>
#include <iostream>
#include <string>
#include <vector>
using namespace v8;
using std::string;
using namespace node;


#define NODEPY_ACCESSOR(fn)         \
        static Handle<Value> fn##Accessor(Local<String> property, const AccessorInfo& info) { \
            HandleScope scope;  \
            Handle<FunctionTemplate> t = FunctionTemplate::New(fn);   \
            return scope.Close(t->GetFunction());   \
        };

class PythonObject : public ObjectWrap {
    PyObject* _object;

    static Persistent<FunctionTemplate> python_function_template_;
    public:
        PythonObject (PyObject* obj) : _object(obj) {
        };

        virtual ~PythonObject() {
            Py_XDECREF(_object);
        };

        static void Initialize(Handle<Object> target) {
            HandleScope scope;


            Local<FunctionTemplate> pytpl = FunctionTemplate::New();
            MakePythonObjectTemplate(pytpl);
            python_function_template_ = Persistent<FunctionTemplate>::New(pytpl);
            
            Handle<FunctionTemplate> t = FunctionTemplate::New(New);
            target->Set(String::NewSymbol("import"), t->GetFunction());

        };

        static Handle<ObjectTemplate> MakePythonObjectTemplate(Local<FunctionTemplate>& t) {
            HandleScope scope;
            Handle<ObjectTemplate> result = t->InstanceTemplate();
            result->SetInternalFieldCount(1);
            result->SetNamedPropertyHandler(MapGet, MapSet);

            Handle<FunctionTemplate> to_string = FunctionTemplate::New(ToString);
            Handle<FunctionTemplate> value_of = FunctionTemplate::New(ValueOf);
            Handle<FunctionTemplate> get_attribute = FunctionTemplate::New(GetAttribute);
            Handle<FunctionTemplate> call = FunctionTemplate::New(Call);
            result->SetAccessor(String::NewSymbol("toString"), ToStringAccessor);
            result->SetAccessor(String::NewSymbol("valueOf"), ValueOfAccessor);
            result->SetAccessor(String::NewSymbol("call"), CallAccessor);
            result->SetCallAsFunctionHandler(Call, Handle<Value>());
            //result->SetAccessor(String::New("getAttribute"), get_attribute->GetFunction());
            //result->SetAccessor(String::New("CALL_NON_FUNCTION"), call->GetFunction());
        };

        NODEPY_ACCESSOR(ToString)
        static Handle<Value> ToString(const Arguments& args) {
            HandleScope scope;
            Handle<Object> obj = args.This();
            PythonObject* py_obj = ObjectWrap::Unwrap<PythonObject>(obj);
            std::string result = py_obj->GetStr();
            return scope.Close(String::New(result.c_str()));
        };

        NODEPY_ACCESSOR(ValueOf)
        static Handle<Value> ValueOf(const Arguments& args) {
            HandleScope scope;
            return scope.Close(ValueOf(args.This()));
        }

        static Handle<Value> ValueOf(const Handle<Object>& obj) {
            HandleScope scope;
            PythonObject* jspy = ObjectWrap::Unwrap<PythonObject>(obj);
            PyObject* py_obj = jspy->GetValue();
            if(PyCallable_Check(py_obj)) {
                Local<FunctionTemplate> call = FunctionTemplate::New(Call);
                return scope.Close(call->GetFunction());
            } else if (PyNumber_Check(py_obj)) {
                long long_result = PyLong_AsLong(py_obj);
                return scope.Close(Integer::New(long_result));
            } else if (PySequence_Check(py_obj)) {
                int len = PySequence_Length(py_obj);
                Local<Array> array = Array::New(len);
                for(int i = 0; i < len; ++i) {
                    Handle<Object> jsobj = python_function_template_->GetFunction()->NewInstance();
                    PyObject* py_obj_out = PySequence_GetItem(py_obj, i);
                    PythonObject* obj_out = new PythonObject(py_obj_out);
                    jsobj->SetInternalField(0, External::New(obj_out));
                    array->Set(i, jsobj);
                }
                return scope.Close(array);
            } else if (PyMapping_Check(py_obj)) {
                int len = PyMapping_Length(py_obj);
                Local<Object> object = Object::New();
                PyObject* keys = PyMapping_Keys(py_obj);
                PyObject* values = PyMapping_Values(py_obj);
                for(int i = 0; i < len; ++i) {
                    PyObject *key = PySequence_GetItem(keys, i),
                        *value = PySequence_GetItem(values, i),
                        *key_as_string = PyObject_Str(key);
                    char* cstr = PyString_AsString(key_as_string);

                    Local<Object> jsobj = python_function_template_->GetFunction()->NewInstance();
                    PythonObject* obj_out = new PythonObject(value);
                    jsobj->SetInternalField(0, External::New(obj_out));
                    object->Set(String::New(cstr), jsobj); 
                    Py_XDECREF(key);
                    Py_XDECREF(key_as_string);
                }
                Py_XDECREF(keys);
                Py_XDECREF(values);
                return scope.Close(object);
            }
            return Undefined();
        };

        NODEPY_ACCESSOR(Call)
        static Handle<Value> Call(const Arguments& args) {
            HandleScope scope;
            Handle<Object> obj = args.This();
            PythonObject* py_obj = ObjectWrap::Unwrap<PythonObject>(obj);
            Local<Value> result = py_obj->CallPython(args);
            return scope.Close(result);
        };

        PyObject* ConvertArgToPyObject(const Handle<Value>& value) {
            int len;
            if(value->IsString()) {
                return PyString_FromString(*String::Utf8Value(value->ToString()));
            } else if(value->IsNumber()) {
                return PyFloat_FromDouble(value->NumberValue());
            } else if(value->IsObject()) {
                Local<Object> obj = value->ToObject();
                if(!obj->FindInstanceInPrototypeChain(python_function_template_).IsEmpty()) {
                    PythonObject* python_object = ObjectWrap::Unwrap<PythonObject>(value->ToObject());
                    PyObject* pyobj = python_object->GetValue();
                    return pyobj;
                } else {
                    Local<Array> property_names = obj->GetPropertyNames();
                    len = property_names->Length();
                    PyObject* py_dict = PyDict_New();
                    for(int i = 0; i < len; ++i) {
                        Local<String> str = property_names->Get(i)->ToString();
                        Local<Value> js_val = obj->Get(str);
                        PyDict_SetItemString(py_dict, *String::Utf8Value(str), ConvertArgToPyObject(js_val));
                    }
                    return py_dict;
                }
                return NULL;
            } else if(value->IsArray()) {
                Local<Array> array = Array::Cast(*value);
                len = array->Length();
                PyObject* py_list = PyList_New(len);
                for(int i = 0; i < len; ++i) {
                    Local<Value> js_val = array->Get(i);
                    PyList_SET_ITEM(py_list, i, ConvertArgToPyObject(js_val));
                }
                return py_list;
            } else if(value->IsUndefined()) {
                Py_INCREF(Py_None);
                return Py_None;
            }
            return NULL;
        }

        Local<Value> CallPython(const Arguments& args) {
            HandleScope scope;
            int len = args.Length();
            PyObject* py_args_as_list = PyTuple_New(len);
            std::vector<PyObject*> py_args_to_decref(len);
            for(int i = 0; i < len; ++i) {
                PyObject* py_arg_n = ConvertArgToPyObject(args[i]);
                PyTuple_SET_ITEM(py_args_as_list, i, py_arg_n);
                py_args_to_decref.push_back(py_arg_n);
            }
            PyObject* py_result = PyObject_CallObject(_object, py_args_as_list);
            if(!py_result) {
                PyErr_Print();
                return scope.Close(ThrowException(
                    Exception::TypeError(String::New("UH OH WILL ROGER"))
                )); 
            }
            for(std::vector<PyObject*>::iterator i = py_args_to_decref.begin(); i != py_args_to_decref.end(); ++i) {
                //Py_XDECREF(*i);
            }

            Local<Object> result = python_function_template_->GetFunction()->NewInstance();
            result->SetInternalField(0, External::New(new PythonObject(py_result)));
            return scope.Close(result);
        }

        PyObject* GetValue() {
            return _object;
        };

        std::string GetStr() {
            PyObject* as_string = PyObject_Str(_object);
            char* cstr = PyString_AsString(as_string);
            Py_XDECREF(_object);
            return std::string(cstr);
        };

        static Handle<Value> New(const Arguments& args) {
            HandleScope scope;
            PythonObject *py_object; 
            PyObject *py_module, *py_module_name;
            if (args.Length() == 0 || !args[0]->IsString()) {
                return ThrowException(
                    Exception::TypeError(String::New("First argument must be a string"))
                );
            }
            String::Utf8Value utf_module_name(args[0]->ToString());
            py_module_name = PyString_FromString(*utf_module_name),
            py_module = PyImport_Import(py_module_name);
            Py_XDECREF(py_module_name);
            if(py_module != NULL) {
                Local<Object> result = python_function_template_->GetFunction()->NewInstance();
                result->SetInternalField(0, External::New(new PythonObject(py_module)));
                return scope.Close(result);
            }
            return ThrowException(
                Exception::TypeError(String::New("Could not import that module."))
            );
        };

        static Handle<Value> MapGet(Local<String> key, const AccessorInfo& info) {
            HandleScope scope;
            Handle<Object> v8this = info.Holder();
            PythonObject* object = ObjectWrap::Unwrap<PythonObject>(v8this),
                *new_object;

            String::Utf8Value utf_attr_name(key);
            std::string key_string(*utf_attr_name);
            PyObject* attr = object->GetAttr(key_string);
            Local<Object> result;
            if (attr) {
                result = python_function_template_->GetFunction()->NewInstance();
                PythonObject* obj_out = new PythonObject(attr);
                result->SetInternalField(0, External::New(obj_out));
                return scope.Close(result);
            }
            return Handle<Value>(); 
        };

        static Handle<Value> MapSet(Local<String> key, Local<Value> value, const AccessorInfo& info) {
            // for now.
            return Undefined();
        };

        static Handle<Value> GetAttribute(const Arguments& args) {
            HandleScope scope;
            Handle<Object> v8this = args.This();
            PythonObject* object = ObjectWrap::Unwrap<PythonObject>(v8this),
                *new_object;

            String::Utf8Value utf_attr_name(args[0]->ToString());
            std::string key_string(*utf_attr_name);
            PyObject* attr = object->GetAttr(key_string);
            Handle<Object> result;
            if (attr) {
                result = python_function_template_->GetFunction()->NewInstance();
                PythonObject* obj_out = new PythonObject(attr);
                result->SetInternalField(0, External::New(obj_out));
                return scope.Close(result);
            }
            return Undefined();
        };

        PyObject* GetAttr(const std::string& attr) {
            if(PyObject_HasAttrString(_object, attr.c_str())) {
                PyObject* python_attribute = PyObject_GetAttrString(_object, attr.c_str());
                if(python_attribute != NULL) {
                    return python_attribute;
                }
            }
            return (PyObject*)NULL;
        };
}; 

Persistent<FunctionTemplate> PythonObject::python_function_template_;

static Handle<Value>
Shutdown (const Arguments& args) {
    Py_Finalize();
    return Undefined();
}

extern "C" void
init (Handle<Object> target) {
    HandleScope scope;
    Py_Initialize();
    // create a new function template and assign it to the exported variable "fibonacci"

    PythonObject::Initialize(target);
    Local<FunctionTemplate> shutdown = FunctionTemplate::New(Shutdown);
    target->Set(String::New("shutdown"), shutdown->GetFunction());
}
