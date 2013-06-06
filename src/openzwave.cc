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

using namespace v8;

/*
 * Initialize the OpenZWave Manager.
 */
Handle<Value> Start(const Arguments& args)
{
	HandleScope scope;

	OpenZWave::Options::Create( "deps/open-zwave/config/", "", "" );
	OpenZWave::Options::Get()->Lock();
	OpenZWave::Manager::Create();

	return scope.Close(Undefined());
}

void init(Handle<Object> target)
{

	target->Set(String::NewSymbol("start"),
	    FunctionTemplate::New(Start)->GetFunction());
}

NODE_MODULE(openzwave, init)
