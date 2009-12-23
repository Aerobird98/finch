#include "Object.h"
#include "BlockObject.h"
#include "Environment.h"
#include "DynamicObject.h"
#include "NumberObject.h"
#include "Scope.h"
#include "StringObject.h"

namespace Finch
{
    using std::ostream;
    
    Ref<Object> Object::NewObject(Ref<Object> prototype, String name)
    {
        return Ref<Object>(new DynamicObject(prototype, name));
    }
    
    Ref<Object> Object::NewObject(Ref<Object> prototype)
    {
        return Ref<Object>(new DynamicObject(prototype));
    }
    
    Ref<Object> Object::NewNumber(Environment & env, double value)
    {
        return Ref<Object>(new NumberObject(env.Number(), value));
    }
    
    Ref<Object> Object::NewString(Environment & env, String value)
    {
        return Ref<Object>(new StringObject(env.String(), value));
    }
    
    Ref<Object> Object::NewBlock(Environment & env, vector<String> params,
                                 Ref<Expr> value)
    {
        return Ref<Object>(new BlockObject(env.Block(), params, value,
                                           // create a closure from the scope
                                           // where the block is defined
                                           env.CurrentScope()));
    }
    
    Ref<Object> Object::Receive(Ref<Object> thisRef, Environment & env,
                                String message, const vector<Ref<Object> > & args)
    {
        // walk up the prototype chain
        if (!mPrototype.IsNull())
        {
            // we're using thisRef and not the prototype's own this reference
            // on purpose. this way, if you send a "copy" message to some
            // object a few links down the prototype chain from Object, you'll
            // get a copy of *that* object, and not Object itself where "copy"
            // is implemented.
            return mPrototype->Receive(thisRef, env, message, args);
        }
        
        //### bob: should do some sort of message not handled thing here
        return Ref<Object>();
    }
    
    Ref<Scope> Object::ObjectScope() const
    {
        return Ref<Scope>();
    }

    ostream & operator<<(ostream & cout, const Object & object)
    {
        object.Trace(cout);
        return cout;
    }
}