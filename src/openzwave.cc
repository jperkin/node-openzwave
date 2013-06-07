/*
 * Copyright (c) 2013 Jonathan Perkin <jonathan@perkin.org.uk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>

#include <node.h>
#include <v8.h>

#include "Manager.h"
#include "Options.h"

struct OpenBaton {
public:
	char path[1024];
	char configDir[1024];
	v8::Persistent<v8::Value> callback;
	v8::Persistent<v8::Value> dataCallback;
	v8::Persistent<v8::Value> disconnectedCallback;
	v8::Persistent<v8::Value> errorCallback;
	int result;
	int saveLogLevel;
	bool consoleOutput;
	char errorString[1024];
};

v8::Handle<v8::Value> Open(const v8::Arguments& args);
void EIO_Open(uv_work_t* req);
void EIO_AfterOpen(uv_work_t* req);
void AfterOpenSuccess(int fd, v8::Handle<v8::Value> dataCallback, v8::Handle<v8::Value> disconnectedCallback, v8::Handle<v8::Value> errorCallback);

/*
 * Initialize the OpenZWave Manager.
 */
v8::Handle<v8::Value> Open(const v8::Arguments& args)
{
	v8::HandleScope scope;

	/*
	 * arg0 = Device Path
	 */
	if(!args[0]->IsString()) {
		return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("First argument must be a string"))));
	}
	v8::String::Utf8Value path(args[0]->ToString());

	/*
	 * arg1 = Options object
	 */
	if(!args[1]->IsObject()) {
		return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("Second argument must be an object"))));
	}
	v8::Local<v8::Object> options = args[1]->ToObject();

	/*
	 * arg2 = Callback
	 */
	if(!args[2]->IsFunction()) {
		return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("Third argument must be a function"))));
	}
	v8::Local<v8::Value> callback = args[2];

	/*
	 * Pass the baton
	 */
	OpenBaton* baton = new OpenBaton();
	memset(baton, 0, sizeof(OpenBaton));

	v8::String::Utf8Value confpath(options->Get(v8::String::New("configDir"))->ToString());

	strcpy(baton->path, *path);
	strcpy(baton->configDir, *confpath);
	baton->consoleOutput = options->Get(v8::String::New("consoleOutput"))->ToBoolean()->BooleanValue();
	baton->saveLogLevel = options->Get(v8::String::New("saveLogLevel"))->ToNumber()->NumberValue();
	baton->callback = v8::Persistent<v8::Value>::New(callback);
	baton->dataCallback = v8::Persistent<v8::Value>::New(options->Get(v8::String::New("dataCallback")));
	baton->disconnectedCallback = v8::Persistent<v8::Value>::New(options->Get(v8::String::New("disconnectedCallback")));
	baton->errorCallback = v8::Persistent<v8::Value>::New(options->Get(v8::String::New("errorCallback")));

	uv_work_t* req = new uv_work_t();
	req->data = baton;
	uv_queue_work(uv_default_loop(), req, EIO_Open, (uv_after_work_cb)EIO_AfterOpen);

	return scope.Close(v8::Undefined());
}

void EIO_Open(uv_work_t* req)
{
	OpenBaton* data = static_cast<OpenBaton*>(req->data);

	OpenZWave::Options::Create(data->configDir, "", "");
	OpenZWave::Options::Get()->AddOptionBool("ConsoleOutput", data->consoleOutput);
	OpenZWave::Options::Get()->AddOptionInt("SaveLogLevel", data->saveLogLevel);
	OpenZWave::Options::Get()->Lock();
	OpenZWave::Manager::Create();

	OpenZWave::Manager::Get()->AddDriver(data->path);
}

void EIO_AfterOpen(uv_work_t* req)
{
	OpenBaton* data = static_cast<OpenBaton*>(req->data);

	v8::Handle<v8::Value> argv[2];

	if (data->errorString[0]) {
		argv[0] = v8::Exception::Error(v8::String::New(data->errorString));
		argv[1] = v8::Undefined();
	} else {
		argv[0] = v8::Undefined();
		argv[1] = v8::Int32::New(data->result);
		AfterOpenSuccess(data->result, data->dataCallback, data->disconnectedCallback, data->errorCallback);
	}
	v8::Function::Cast(*data->callback)->Call(v8::Context::GetCurrent()->Global(), 2, argv);

	data->callback.Dispose();
	delete data;
	delete req;
}

void AfterOpenSuccess(int fd, v8::Handle<v8::Value> dataCallback, v8::Handle<v8::Value> disconnectedCallback, v8::Handle<v8::Value> errorCallback)
{
}

extern "C" {
void init(v8::Handle<v8::Object> target)
{
	v8::HandleScope scope;

	NODE_SET_METHOD(target, "open", Open);
}
}

NODE_MODULE(openzwave, init)
