#include <iostream>

#include "BlockObject.h"
#include "CodeBlock.h"
#include "Environment.h"
#include "Interpreter.h"

namespace Finch
{
    using std::cout;
    using std::endl;
    
    Interpreter::Interpreter(Environment & environment)
    :   mIsRunning(true),
        mEnvironment(environment),
        mLoopCode(vector<String>()),
        mDiscardCode(vector<String>())
    {
        // build the special "while loop" chunk of bytecode
        mLoopCode.Write(OP_LOOP_1);
        mLoopCode.Write(OP_LOOP_2);
        mLoopCode.Write(OP_LOOP_3);
        mLoopCode.Write(OP_LOOP_4);
        mLoopCode.Write(OP_END_BLOCK);
        
        mDiscardCode.Write(OP_POP);
        mDiscardCode.Write(OP_END_BLOCK);
    }

    Ref<Object> Interpreter::Execute(const CodeBlock & code)
    {
        // push the starting block
        mCallStack.Push(CallFrame(&code, mEnvironment.Globals(), mEnvironment.Nil()));
        
        // continue processing bytecode until the entire callstack has
        // completed
        while (mCallStack.Count() > 0)
        {
            CallFrame & frame = mCallStack.Peek();
            const Instruction & instruction = (*frame.code)[frame.address];
            
            switch (instruction.op)
            {
                case OP_NOTHING:
                    // do nothing
                    break;
                    
                case OP_NUMBER_LITERAL:
                    PushOperand(Object::NewNumber(mEnvironment, instruction.arg.number));
                    break;
                    
                case OP_STRING_LITERAL:
                    {
                        String string = mEnvironment.Strings().Find(instruction.arg.id);
                        PushOperand(Object::NewString(mEnvironment, string));
                    }
                    break;
                    
                case OP_BLOCK_LITERAL:
                    {
                        // capture the current scope
                        Ref<Scope> closure = mCallStack.Peek().scope;
                        
                        const CodeBlock & code = mEnvironment.Blocks().Find(instruction.arg.id);
                        Ref<Object> block = Object::NewBlock(mEnvironment, code, closure);
                        PushOperand(block);
                    }
                    break;
                    
                case OP_POP:
                    PopOperand();
                    break;
                    
                case OP_DEF_GLOBAL:
                    {
                        // def returns the defined value, so instead of popping
                        // and then pushing the value back on the stack, we'll
                        // just peek
                        Ref<Object> value = mOperands.Peek();
                        //### bob: if we get strings fully interned (i.e. no dupes in
                        // string table), then the global name scope doesn't need the
                        // actual string at all, just the id in the string table
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        mEnvironment.Globals()->Define(name, value);                        
                    }
                    break;
                    
                case OP_DEF_OBJECT:
                    {
                        // def returns the defined value, so instead of popping
                        // and then pushing the value back on the stack, we'll
                        // just peek
                        Ref<Object> value = mOperands.Peek();
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        if (!Self().IsNull())
                        {
                            Self()->ObjectScope()->Define(name, value);
                        }
                    }
                    break;
                    
                case OP_DEF_LOCAL:
                    {
                        // def returns the defined value, so instead of popping
                        // and then pushing the value back on the stack, we'll
                        // just peek
                        Ref<Object> value = mOperands.Peek();
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        CurrentScope()->Define(name, value);
                    }
                    break;
                    
                case OP_SET_LOCAL:
                    {
                        // def returns the defined value, so instead of popping
                        // and then pushing the value back on the stack, we'll
                        // just peek
                        Ref<Object> value = mOperands.Peek();
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        CurrentScope()->Set(name, value);
                    }
                    break;
                    
                case OP_LOAD_GLOBAL:
                    {
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        Ref<Object> value = mEnvironment.Globals()->LookUp(name);
                        PushOperand(value.IsNull() ? mEnvironment.Nil() : value);
                    }
                    break;
                    
                case OP_LOAD_OBJECT:
                    {
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        if (!Self().IsNull())
                        {
                            Ref<Object> value = Self()->ObjectScope()->LookUp(name);
                            PushOperand(value.IsNull() ? mEnvironment.Nil() : value);
                        }
                        else
                        {
                            PushOperand(Ref<Object>());
                        }
                    }
                    break;
                    
                case OP_LOAD_LOCAL:
                    {
                        String name = mEnvironment.Strings().Find(instruction.arg.id);
                        
                        if (name == "self")
                        {
                            PushOperand(mCallStack.Peek().self);
                        }
                        else
                        {
                            Ref<Object> value = CurrentScope()->LookUp(name);
                            PushOperand(value.IsNull() ? mEnvironment.Nil() : value);
                        }
                    }
                    break;
                    
                case OP_MESSAGE_0:
                case OP_MESSAGE_1:
                case OP_MESSAGE_2:
                case OP_MESSAGE_3:
                case OP_MESSAGE_4:
                case OP_MESSAGE_5:
                case OP_MESSAGE_6:
                case OP_MESSAGE_7:
                case OP_MESSAGE_8:
                case OP_MESSAGE_9:
                case OP_MESSAGE_10:
                    {
                        // pop the arguments
                        vector<Ref<Object> > args;
                        for (int i = 0; i < instruction.op - OP_MESSAGE_0; i++)
                        {
                            args.push_back(PopOperand());
                        }
                        
                        // reverse them since the stack has them in order (so
                        // that arguments are evaluated from left to right) and
                        // popping reverses the order
                        reverse(args.begin(), args.end());
                        
                        // send the message
                        String string = mEnvironment.Strings().Find(instruction.arg.id);
                        Ref<Object> receiver = PopOperand();
                        
                        receiver->Receive(receiver, *this, string, args);
                    }
                    break;
                    
                    // these next for opcodes handle the one built-in loop
                    // construct: "while". because a while loop must wait for
                    // the condition to be evaluated, and then later the body,
                    // it proceeds in stages, with an opcode for each stage.
                    //
                    // OP_LOOP_1 begins evaluating the condition expression
                    // OP_LOOP_2 checks the result of that and either ends the
                    //           loop or continues
                    // OP_LOOP_3 begins evaluating the body
                    // OP_LOOP_4 discards the result of that and loops back to
                    //           the beginning by explicitly changing the
                    //           instruction pointer
                    //
                    // note that all of this is initiated by a call to
                    // WhileLoop on the interpreter. that pushes a special
                    // static CodeBlock that contains this sequence of opcodes.
                    // we do this, instead of compiling a while loop directly
                    // into the bytecode where it appears so that it's still
                    // possible to overload while:do: at runtime.
                    
                case OP_LOOP_1:
                    {
                        // evaluate the conditional (while leaving it on the stack)
                        Ref<Object> condition = mOperands.Peek();
                        condition->Receive(condition, *this, "call", vector<Ref<Object> >());
                    }
                    break;

                case OP_LOOP_2:
                    // if the condition is false, end the loop
                    if (PopOperand() != mEnvironment.True())
                    {
                        // pop the condition and body blocks
                        PopOperand();
                        PopOperand();
                        
                        // end the loop
                        mCallStack.Pop();
                        
                        // every expression must return something
                        PushNil();
                    }
                    break;
                    
                case OP_LOOP_3:
                    {
                        // evaluate the body
                        Ref<Object> body = mOperands[1];
                        body->Receive(body, *this, "call", vector<Ref<Object> >());
                    }
                    break;
                    
                case OP_LOOP_4:
                    // discard the body's return value
                    PopOperand();
                    
                    // restart the loop
                    frame.address = -1; // the ++ later will get us to 0
                    break;
                    
                case OP_END_BLOCK:
                    mCallStack.Pop();
                    break;
                    
                default:
                    ASSERT(false, "Unknown op code.");
            }
            
            // advance to the next instruction
            frame.address++;
        }
        
        // there should be one object left on the stack: the final return
        return PopOperand();
    }
    
    void Interpreter::Push(Ref<Object> object)
    {
        PushOperand(object);
    }
    
    void Interpreter::PushNil()
    {
        Push(mEnvironment.Nil());
    }

    void Interpreter::PushBool(bool value)
    {
        PushOperand(value ? mEnvironment.True() : mEnvironment.False());
    }

    void Interpreter::PushNumber(double value)
    {
        Push(Object::NewNumber(mEnvironment, value));
    }

    void Interpreter::CallBlock(const BlockObject & block,
                                const vector<Ref<Object> > & args)
    {
        // continue using the current self object
        Ref<Object> self = mCallStack.Peek().self;
        
        CallMethod(self, block, args);
    }
    
    void Interpreter::CallMethod(Ref<Object> self,
                                 const BlockObject & block,
                                 const vector<Ref<Object> > & args)
    {
        // make sure we have the right number of arguments
        //### bob: could change to ignore extra args and pad missing ones with
        // nil if we want to be "looser" about calling convention
        if (block.Params().size() != args.size())
        {
            RuntimeError(String::Format("Block expects %d arguments, but was passed %d.",
                                        block.Params().size(), args.size()));
            PushNil();
            return;
        }
        
        // create a new local scope for the block
        Ref<Scope> scope = Ref<Scope>(new Scope(block.Closure()));
        
        // bind the arguments
        for (unsigned int i = 0; i < args.size(); i++)
        {
            scope->Define(block.Params()[i], args[i]);
        }
        
        // push the call onto the stack
        mCallStack.Push(CallFrame(&block.GetCode(), scope, self));
    }
    
    void Interpreter::WhileLoop(Ref<Object> condition, Ref<Object> body)
    {
        // push the arguments onto the stack
        Push(body);
        Push(condition);
        
        // call our special loop "function"
        mCallStack.Push(CallFrame(&mLoopCode, mCallStack.Peek().scope, mCallStack.Peek().self));
    }
    
    void Interpreter::DiscardReturn()
    {
        // call our special pop "function"
        mCallStack.Push(CallFrame(&mDiscardCode, mCallStack.Peek().scope, mCallStack.Peek().self));
    }

    void Interpreter::RuntimeError(const String & message)
    {
        //### bob: ideally, this should be programmatically configurable from
        // within Finch
        cout << "Runtime error: " << message << endl;
    }
    
    void Interpreter::PushOperand(Ref<Object> object)
    {
        ASSERT(!object.IsNull(), "Cannot push a null object. (Should be Nil instead.)");
        
        //std::cout << "push " << object << std::endl;
        mOperands.Push(object);
    }
    
    Ref<Object> Interpreter::PopOperand()
    {
        Ref<Object> object = mOperands.Pop();
        
        //std::cout << "pop  " << object << std::endl;
        return object;
    }
}