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
#include <uv.h>
#include <pthread.h>
#include <list>
#include <queue>

#include <v8.h>
#include <node_object_wrap.h>
#include <node_internals.h>

#include "Manager.h"
#include "Node.h"
#include "Notification.h"
#include "Options.h"
#include "Value.h"

using namespace v8;
using namespace node;

namespace {
struct OZW : node::ObjectWrap {
   static void New(const FunctionCallbackInfo<Value>& args);
   static void Connect(const FunctionCallbackInfo<Value>& args);
   static void Disconnect(const FunctionCallbackInfo<Value>& args);
   static void SetValue(const FunctionCallbackInfo<Value>& args);
   static void SetLevel(const FunctionCallbackInfo<Value>& args);
   static void SetLocation(const FunctionCallbackInfo<Value>& args);
   static void SetName(const FunctionCallbackInfo<Value>& args);
   static void SwitchOn(const FunctionCallbackInfo<Value>& args);
   static void SwitchOff(const FunctionCallbackInfo<Value>& args);
   static void EnablePoll(const FunctionCallbackInfo<Value>& args);
   static void DisablePoll(const FunctionCallbackInfo<Value>& args);
   static void HardReset(const FunctionCallbackInfo<Value>& args);
   static void SoftReset(const FunctionCallbackInfo<Value>& args);
};

Persistent<Object> context_obj;

uv_async_t async;

typedef struct {
   uint32_t			type;
   uint32_t			homeid;
   uint8_t				nodeid;
   uint8_t				groupidx;
   uint8_t				event;
   uint8_t				buttonid;
   uint8_t				sceneid;
   uint8_t				notification;
   std::list<OpenZWave::ValueID>	values;
} NotifInfo;

typedef struct {
   uint32_t			homeid;
   uint8_t				nodeid;
   bool				polled;
   std::list<OpenZWave::ValueID>	values;
} NodeInfo;

/*
 * Message passing queue between OpenZWave callback and v8 async handler.
 */
static pthread_mutex_t zqueue_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::queue<NotifInfo *> zqueue;

/*
 * Node state.
 */
static pthread_mutex_t znodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::list<NodeInfo *> znodes;

static uint32_t homeid;

/*
 * Return the node for this request.
 */
NodeInfo *get_node_info(uint8_t nodeid)
{
   std::list<NodeInfo *>::iterator it;
   NodeInfo *node;

   for (it = znodes.begin(); it != znodes.end(); ++it) {
      node = *it;
      if (node->nodeid == nodeid)
         return node;
   }

   return NULL;
}

/*
 * OpenZWave callback, just push onto queue and trigger the handler
 * in v8 land.
 */
void cb(OpenZWave::Notification const *cb, void *ctx)
{
   NotifInfo *notif = new NotifInfo();

   notif->type = cb->GetType();
   notif->homeid = cb->GetHomeId();
   notif->nodeid = cb->GetNodeId();
   notif->values.push_front(cb->GetValueID());

   /*
    * Some values are only set on particular notifications, and
    * assertions in openzwave prevent us from trying to fetch them
    * unconditionally.
    */
   switch (notif->type) {
   case OpenZWave::Notification::Type_Group:
      notif->groupidx = cb->GetGroupIdx();
      break;
   case OpenZWave::Notification::Type_NodeEvent:
      notif->event = cb->GetEvent();
      break;
   case OpenZWave::Notification::Type_CreateButton:
   case OpenZWave::Notification::Type_DeleteButton:
   case OpenZWave::Notification::Type_ButtonOn:
   case OpenZWave::Notification::Type_ButtonOff:
      notif->buttonid = cb->GetButtonId();
      break;
   case OpenZWave::Notification::Type_SceneEvent:
      notif->sceneid = cb->GetSceneId();
      break;
   case OpenZWave::Notification::Type_Notification:
      notif->notification = cb->GetNotification();
      break;
   }

   pthread_mutex_lock(&zqueue_mutex);
   zqueue.push(notif);
   pthread_mutex_unlock(&zqueue_mutex);

   uv_async_send(&async);
}

/*
 * Async handler, triggered by the OpenZWave callback.
 */
void async_cb_handler(uv_async_t *handle)
{

   Isolate* isolate = Isolate::GetCurrent();

   NodeInfo *node;
   NotifInfo *notif;
   Local<Value> args[16];

   pthread_mutex_lock(&zqueue_mutex);

   while (!zqueue.empty())
   {
      notif = zqueue.front();

      switch (notif->type) {
      case OpenZWave::Notification::Type_DriverReady:
         homeid = notif->homeid;
         args[0] = String::NewFromUtf8(isolate, "driver ready");
         args[1] = Integer::New(isolate,homeid);
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 2, args);
         break;
      case OpenZWave::Notification::Type_DriverFailed:
         args[0] = String::NewFromUtf8(isolate,"driver failed");
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 1, args);
         break;
      /*
       * NodeNew is triggered when a node is discovered which is not
       * found in the OpenZWave XML file.  As we do not use that file
       * simply ignore those notifications for now.
       *
       * NodeAdded is when we actually have a new node to set up.
       */
      case OpenZWave::Notification::Type_NodeNew:
         break;
      case OpenZWave::Notification::Type_NodeAdded:
         node = new NodeInfo();
         node->homeid = notif->homeid;
         node->nodeid = notif->nodeid;
         node->polled = false;
         pthread_mutex_lock(&znodes_mutex);
         znodes.push_back(node);
         pthread_mutex_unlock(&znodes_mutex);
         args[0] = String::NewFromUtf8(isolate, "node added");
         args[1] = Integer::New(isolate,notif->nodeid);
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 2, args);
         break;
      /*
       * Ignore intermediate notifications about a node status, we
       * wait until the node is ready before retrieving information.
       */
      case OpenZWave::Notification::Type_NodeProtocolInfo:
      case OpenZWave::Notification::Type_NodeNaming:
      // XXX: these should be supported correctly.
      case OpenZWave::Notification::Type_PollingEnabled:
      case OpenZWave::Notification::Type_PollingDisabled:
         break;
      /*
       * Node values.
       */
      case OpenZWave::Notification::Type_ValueAdded:
      case OpenZWave::Notification::Type_ValueChanged:
      {
         OpenZWave::ValueID value = notif->values.front();
         Local<Object> valobj = Object::New(isolate);
         const char *evname = (notif->type == OpenZWave::Notification::Type_ValueAdded)
             ? "value added" : "value changed";

         if (notif->type == OpenZWave::Notification::Type_ValueAdded) {
            if ((node = get_node_info(notif->nodeid))) {
               pthread_mutex_lock(&znodes_mutex);
               node->values.push_back(value);
               pthread_mutex_unlock(&znodes_mutex);
            }
            OpenZWave::Manager::Get()->SetChangeVerified(value, true);
         }

         
         /*
          * Common value types.
          */
         valobj->Set(String::NewFromUtf8(isolate, "type", String::kInternalizedString),
                String::NewFromUtf8(isolate, OpenZWave::Value::GetTypeNameFromEnum (value.GetType())));

         valobj->Set(String::NewFromUtf8(isolate, "genre", String::kInternalizedString),
                String::NewFromUtf8(isolate, OpenZWave::Value::GetGenreNameFromEnum(value.GetGenre())));

         valobj->Set(String::NewFromUtf8(isolate, "instance", String::kInternalizedString),
                Integer::New(isolate,value.GetInstance()));

         valobj->Set(String::NewFromUtf8(isolate, "index", String::kInternalizedString),
                Integer::New(isolate, value.GetIndex()));

         valobj->Set(String::NewFromUtf8(isolate, "label", String::kInternalizedString),
                String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetValueLabel(value).c_str()));

         valobj->Set(String::NewFromUtf8(isolate, "units", String::kInternalizedString),
                String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetValueUnits(value).c_str()));

         valobj->Set(String::NewFromUtf8(isolate, "read_only", String::kInternalizedString),
                Boolean::New(isolate,OpenZWave::Manager::Get()->IsValueReadOnly(value))->ToBoolean());

         valobj->Set(String::NewFromUtf8(isolate, "write_only", String::kInternalizedString),
                Boolean::New(isolate,OpenZWave::Manager::Get()->IsValueWriteOnly(value))->ToBoolean());

         // XXX: verify_changes=
         // XXX: poll_intensity=
         valobj->Set(String::NewFromUtf8(isolate, "min", String::kInternalizedString),
                Integer::New(isolate,OpenZWave::Manager::Get()->GetValueMin(value)));

         valobj->Set(String::NewFromUtf8(isolate, "max", String::kInternalizedString),
                Integer::New(isolate,OpenZWave::Manager::Get()->GetValueMax(value)));

         /*
          * The value itself is type-specific.
          */
         switch (value.GetType()) {
         case OpenZWave::ValueID::ValueType_Bool:
         {
            bool val;
            OpenZWave::Manager::Get()->GetValueAsBool(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), Boolean::New(isolate,val)->ToBoolean());
            break;
         }
         case OpenZWave::ValueID::ValueType_Byte:
         {
            uint8_t val;
            OpenZWave::Manager::Get()->GetValueAsByte(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), Integer::New(isolate,val));
            break;
         }
         case OpenZWave::ValueID::ValueType_Decimal:
         {
            float val;
            OpenZWave::Manager::Get()->GetValueAsFloat(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), Integer::New(isolate,val));
            break;
         }
         case OpenZWave::ValueID::ValueType_Int:
         {
            int32_t val;
            OpenZWave::Manager::Get()->GetValueAsInt(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), Integer::New(isolate,val));
            break;
         }
         case OpenZWave::ValueID::ValueType_List:
         {
            Local<Array> items;
         }
         case OpenZWave::ValueID::ValueType_Short:
         {
            int16_t val;
            OpenZWave::Manager::Get()->GetValueAsShort(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), Integer::New(isolate,val));
            break;
         }
         case OpenZWave::ValueID::ValueType_String:
         {
            std::string val;
            OpenZWave::Manager::Get()->GetValueAsString(value, &val);
            valobj->Set(String::NewFromUtf8(isolate, "value", String::kInternalizedString), String::NewFromUtf8(isolate, val.c_str()));
            break;
         }
         /*
          * Buttons do not have a value.
          */
         case OpenZWave::ValueID::ValueType_Button:
         {
            break;
         }
         default:
            fprintf(stderr, "unsupported value type: 0x%x\n", value.GetType());
            break;
         }

         args[0] = String::NewFromUtf8(isolate, evname);
         args[1] = Integer::New(isolate,notif->nodeid);
         args[2] = Integer::New(isolate,value.GetCommandClassId());
         args[3] = valobj;
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 4, args);

         break;
      }
      /*
       * A value update was sent but nothing changed, likely due to
       * the value just being polled.  Ignore, as we handle actual
       * changes above.
       */
      case OpenZWave::Notification::Type_ValueRefreshed:
         break;
      case OpenZWave::Notification::Type_ValueRemoved:
      {
         OpenZWave::ValueID value = notif->values.front();
         std::list<OpenZWave::ValueID>::iterator vit;
         if ((node = get_node_info(notif->nodeid))) {
            for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
               if ((*vit) == notif->values.front()) {
                  node->values.erase(vit);
                  break;
               }
            }
         }
         args[0] = String::NewFromUtf8(isolate, "value removed");
         args[1] = Integer::New(isolate,notif->nodeid);
         args[2] = Integer::New(isolate,value.GetCommandClassId());
         args[3] = Integer::New(isolate,value.GetIndex());
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 4, args);
         break;
      }
      /*
       * I believe this means that the node is now ready to accept
       * commands, however for now we will wait until all queries are
       * complete before notifying upstack, just in case.
       */
      case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
         break;
      /*
       * The node is now fully ready for operation.
       */
      case OpenZWave::Notification::Type_NodeQueriesComplete:
      {
         Local<Object> info = Object::New(isolate);
         info->Set(String::NewFromUtf8(isolate,"manufacturer",String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeManufacturerName(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "manufacturerid", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeManufacturerId(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "product", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeProductName(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "producttype", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeProductType(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "productid", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeProductId(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "type", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeType(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "name", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeName(notif->homeid, notif->nodeid).c_str()));
         info->Set(String::NewFromUtf8(isolate, "loc", String::kInternalizedString),
             String::NewFromUtf8(isolate, OpenZWave::Manager::Get()->GetNodeLocation(notif->homeid, notif->nodeid).c_str()));
         args[0] = String::NewFromUtf8(isolate, "node ready");
         args[1] = Integer::New(isolate, notif->nodeid);
         args[2] = info;
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 3, args);
         break;
      }
      /*
       * The network scan has been completed.  Currently we do not
       * care about dead nodes - is there anything we can do anyway?
       */
      case OpenZWave::Notification::Type_AwakeNodesQueried:
      case OpenZWave::Notification::Type_AllNodesQueried:
      case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
         args[0] = String::NewFromUtf8(isolate, "scan complete");
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 1, args);
         break;
      /*
       * A general notification.
       */
      case OpenZWave::Notification::Type_Notification:
         args[0] = String::NewFromUtf8(isolate, "notification");
         args[1] = Integer::New(isolate,notif->nodeid);
         args[2] = Integer::New(isolate,notif->notification);
         MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 3, args);
         break;
      /*
       * Send unhandled events to stderr so we can monitor them if
       * necessary.
       */
      default:
         fprintf(stderr, "Unhandled notification: %d\n", notif->type);
         break;
      }

      zqueue.pop();
   }

   pthread_mutex_unlock(&zqueue_mutex);
}

void OZW::New(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   assert(args.IsConstructCall());
   OZW* self = new OZW();
   self->Wrap(args.This());

   Local<Object> opts = args[0]->ToObject();
   String::Utf8Value modPath(opts->Get(String::NewFromUtf8(isolate, "modpath"))->ToString());
   std::string confpath = std::string(*modPath);
   confpath += "/../deps/open-zwave/config";

   /*
    * Options are global for all drivers and can only be set once.
    */
   OpenZWave::Options::Create(confpath.c_str(), "", "");
   OpenZWave::Options::Get()->AddOptionBool("ConsoleOutput", opts->Get(String::NewFromUtf8(isolate, "consoleoutput"))->BooleanValue());
   OpenZWave::Options::Get()->AddOptionBool("Logging", opts->Get(String::NewFromUtf8(isolate, "logging"))->BooleanValue());
   OpenZWave::Options::Get()->AddOptionBool("SaveConfiguration", opts->Get(String::NewFromUtf8(isolate, "saveconfig"))->BooleanValue());
   OpenZWave::Options::Get()->AddOptionInt("DriverMaxAttempts", opts->Get(String::NewFromUtf8(isolate, "driverattempts"))->IntegerValue());
   OpenZWave::Options::Get()->AddOptionInt("PollInterval", opts->Get(String::NewFromUtf8(isolate, "pollinterval"))->IntegerValue());
   OpenZWave::Options::Get()->AddOptionBool("IntervalBetweenPolls", true);
   OpenZWave::Options::Get()->AddOptionBool("SuppressValueRefresh", opts->Get(String::NewFromUtf8(isolate, "suppressrefresh"))->BooleanValue());
   OpenZWave::Options::Get()->Lock();

   args.GetReturnValue().Set(args.This());
}

void OZW::Connect(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   std::string path = (*String::Utf8Value(args[0]->ToString()));

   uv_async_init(uv_default_loop(), &async, async_cb_handler);


   context_obj.Reset(isolate, args.This());

   OpenZWave::Manager::Create();
   OpenZWave::Manager::Get()->AddWatcher(cb, NULL);
   OpenZWave::Manager::Get()->AddDriver(path);

   Handle<Value> argv[1] = { String::NewFromUtf8(isolate, "connected") };
   node::MakeCallback(isolate, Local<Object>::New(isolate, context_obj), "emit", 1, argv);

}

void OZW::Disconnect(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   std::string path = (*String::Utf8Value(args[0]->ToString()));

   OpenZWave::Manager::Get()->RemoveDriver(path);
   OpenZWave::Manager::Get()->RemoveWatcher(cb, NULL);
   OpenZWave::Manager::Destroy();
   OpenZWave::Options::Destroy();
}

/*
 * Generic value set.
 */
void OZW::SetValue(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   uint8_t comclass = args[1]->ToNumber()->Value();
   uint8_t index = args[2]->ToNumber()->Value();

   NodeInfo *node;
   std::list<OpenZWave::ValueID>::iterator vit;

   if ((node = get_node_info(nodeid))) {
      for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
         if (((*vit).GetCommandClassId() == comclass) &&
             ((*vit).GetIndex() == index)) {

            switch ((*vit).GetType()) {
            case OpenZWave::ValueID::ValueType_Bool:
            {
               bool val = args[3]->ToBoolean()->Value();
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            case OpenZWave::ValueID::ValueType_Byte:
            {
               uint8_t val = args[3]->ToInteger()->Value();
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            case OpenZWave::ValueID::ValueType_Decimal:
            {
               float val = args[3]->ToNumber()->NumberValue();
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            case OpenZWave::ValueID::ValueType_Int:
            {
               int32_t val = args[3]->ToInteger()->Value();
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            case OpenZWave::ValueID::ValueType_Short:
            {
               int16_t val = args[3]->ToInteger()->Value();
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            case OpenZWave::ValueID::ValueType_String:
            {
               std::string val = (*String::Utf8Value(args[3]->ToString()));
               OpenZWave::Manager::Get()->SetValue(*vit, val);
               break;
            }
            }
         }
      }
   }

}

/*
 * Set a COMMAND_CLASS_SWITCH_MULTILEVEL device to a specific value.
 */
void OZW::SetLevel(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   uint8_t value = args[1]->ToNumber()->Value();

   NodeInfo *node;
   std::list<OpenZWave::ValueID>::iterator vit;

   if ((node = get_node_info(nodeid))) {
      for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
         if ((*vit).GetCommandClassId() == 0x26 && (*vit).GetIndex() == 0) {
            OpenZWave::Manager::Get()->SetValue(*vit, value);
            break;
         }
      }
   }

}

/*
 * Write a new location string to the device, if supported.
 */
void OZW::SetLocation(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   std::string location = (*String::Utf8Value(args[1]->ToString()));

   OpenZWave::Manager::Get()->SetNodeLocation(homeid, nodeid, location);
}

/*
 * Write a new name string to the device, if supported.
 */
void OZW::SetName(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   String::Utf8Value argsName(args[1]->ToString());
   std::string name = (*argsName);

   OpenZWave::Manager::Get()->SetNodeName(homeid, nodeid, name);
}

/*
 * Switch a COMMAND_CLASS_SWITCH_BINARY on/off
 */
void set_switch(uint8_t nodeid, bool state)
{
   NodeInfo *node;
   std::list<OpenZWave::ValueID>::iterator vit;

   if ((node = get_node_info(nodeid))) {
      for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
         if ((*vit).GetCommandClassId() == 0x25) {
            OpenZWave::Manager::Get()->SetValue(*vit, state);
            break;
         }
      }
   }
}
void OZW::SwitchOn(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   set_switch(nodeid, true);
}


void OZW::SwitchOff(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   set_switch(nodeid, false);

}

/*
 * Enable/Disable polling on a COMMAND_CLASS basis.
 */
void OZW::EnablePoll(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   uint8_t comclass = args[1]->ToNumber()->Value();
   NodeInfo *node;
   std::list<OpenZWave::ValueID>::iterator vit;

   if ((node = get_node_info(nodeid))) {
      for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
         if ((*vit).GetCommandClassId() == comclass) {
            OpenZWave::Manager::Get()->EnablePoll((*vit), 1);
            break;
         }
      }
   }
}

void OZW::DisablePoll(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   uint8_t nodeid = args[0]->ToNumber()->Value();
   uint8_t comclass = args[1]->ToNumber()->Value();
   NodeInfo *node;
   std::list<OpenZWave::ValueID>::iterator vit;

   if ((node = get_node_info(nodeid))) {
      for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
         if ((*vit).GetCommandClassId() == comclass) {
            OpenZWave::Manager::Get()->DisablePoll((*vit));
            break;
         }
      }
   }

}

/*
 * Reset the ZWave controller chip.  A hard reset is destructive and wipes
 * out all known configuration, a soft reset just restarts the chip.
 */
void OZW::HardReset(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);

   OpenZWave::Manager::Get()->ResetController(homeid);

   args.GetReturnValue().SetUndefined();
}
void OZW::SoftReset(const FunctionCallbackInfo<Value>& args)
{
   Isolate* isolate = args.GetIsolate();
   HandleScope scope(isolate);
   OpenZWave::Manager::Get()->SoftReset(homeid);
   args.GetReturnValue().SetUndefined();
}

extern "C" void init(Handle<Object> target)
{
   Isolate* isolate = Isolate::GetCurrent();
   HandleScope scope(isolate);

   Local<FunctionTemplate> t = FunctionTemplate::New(isolate,OZW::New);
   t->InstanceTemplate()->SetInternalFieldCount(1);
   t->SetClassName(String::NewFromUtf8(isolate,"OZW"));

   NODE_SET_PROTOTYPE_METHOD(t, "connect", OZW::Connect);
   NODE_SET_PROTOTYPE_METHOD(t, "disconnect", OZW::Disconnect);
   NODE_SET_PROTOTYPE_METHOD(t, "setValue", OZW::SetValue);
   NODE_SET_PROTOTYPE_METHOD(t, "setLevel", OZW::SetLevel);
   NODE_SET_PROTOTYPE_METHOD(t, "setLocation", OZW::SetLocation);
   NODE_SET_PROTOTYPE_METHOD(t, "setName", OZW::SetName);
   NODE_SET_PROTOTYPE_METHOD(t, "switchOn", OZW::SwitchOn);
   NODE_SET_PROTOTYPE_METHOD(t, "switchOff", OZW::SwitchOff);
   NODE_SET_PROTOTYPE_METHOD(t, "enablePoll", OZW::EnablePoll);
   NODE_SET_PROTOTYPE_METHOD(t, "disablePoll", OZW::EnablePoll);
   NODE_SET_PROTOTYPE_METHOD(t, "hardReset", OZW::HardReset);
   NODE_SET_PROTOTYPE_METHOD(t, "softReset", OZW::SoftReset);

   target->Set(String::NewFromUtf8(isolate,"Emitter"), t->GetFunction());
}

}

NODE_MODULE(openzwave, init)
