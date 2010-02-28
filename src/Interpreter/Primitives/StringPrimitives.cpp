#include <iostream>

#include "StringPrimitives.h"
#include "DynamicObject.h"
#include "Environment.h"
#include "Interpreter.h"
#include "Object.h"

namespace Finch
{
    PRIMITIVE(StringAdd)
    {
        //### bob: need to figure out how a primitive can call a non-primitive function
        /*
         // dynamically convert the object to a string
         vector<Ref<Object> > noArgs;
         Ref<Object> toString = args[0]->Receive(args[0], interpreter, "toString", noArgs);
         */
        interpreter.PushString(thisRef->AsString() + args[0]->AsString());
    }
    
    PRIMITIVE(StringLength)
    {
        interpreter.PushNumber(thisRef->AsString().Length());
    }
    
    PRIMITIVE(StringAt)
    {
        String thisString = thisRef->AsString();
        int    index      = static_cast<int>(args[0]->AsNumber());
        
        if ((index >= 0) && (index < thisString.Length()))
        {
            String substring = String(thisString[index]);
            interpreter.PushString(substring);
        }
        else
        {
            // out of bounds
            interpreter.PushNil();
        }
    }

    PRIMITIVE(StringEquals)
    {
        /*
        // dynamically convert the object to a string
        vector<Ref<Object> > noArgs;
        Ref<Object> toString = args[0]->Receive(args[0], interpreter, "toString", noArgs);
        */
        interpreter.PushBool(thisRef->AsString() == args[0]->AsString());
    }
    
    PRIMITIVE(StringNotEquals)
    {
        /*
        // dynamically convert the object to a string
        vector<Ref<Object> > noArgs;
        Ref<Object> toString = args[0]->Receive(args[0], interpreter, "toString", noArgs);
        */
        interpreter.PushBool(thisRef->AsString() != args[0]->AsString());
    }
    
    PRIMITIVE(StringHashCode)
    {
        interpreter.PushNumber(static_cast<double>(thisRef->AsString().HashCode()));
    }
}
