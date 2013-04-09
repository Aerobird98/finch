#include "ArrayExpr.h"
#include "BlockExpr.h"
#include "DefExpr.h"
#include "FinchParser.h"
#include "IErrorReporter.h"
#include "ILineReader.h"
#include "MessageExpr.h"
#include "NameExpr.h"
#include "NumberExpr.h"
#include "ObjectExpr.h"
#include "ReturnExpr.h"
#include "SelfExpr.h"
#include "SequenceExpr.h"
#include "SetExpr.h"
#include "StringExpr.h"
#include "UndefineExpr.h"
#include "VarExpr.h"

namespace Finch
{    
    Ref<Expr> FinchParser::Parse()
    {
        if (IsInfinite())
        {
            // Skip past Sequence() otherwise we'll keep reading lines forever
            // TODO(bob): This is wrong, actually. It means if you enter:
            //   1, 2, 3
            // on the REPL, it will stop after 1. :(
            Ref<Expr> expr = Statement();
            
            // Discard a trailing newline.
            Match(TOKEN_SEMICOLON);
            
            // Don't return anything if we had a parse error.
            if (HadError()) return Ref<Expr>();
            
            return expr;
        }
        else
        {
            // Since expression includes sequence expressions, this will parse
            // as many lines as we have.
            Ref<Expr> expr = Expression();
            Expect(TOKEN_EOF, "Parser ended unexpectedly before reaching end of file.");
            
            // Don't return anything if we had a parse error.
            if (HadError()) return Ref<Expr>();
            
            return expr;
        }
    }
    
    Ref<Expr> FinchParser::Expression()
    {
        Ref<Expr> expr = Sequence();

        // Discard a trailing newline.
        Match(TOKEN_SEMICOLON);
        
        return expr;
    }
    
    Ref<Expr> FinchParser::Sequence()
    {
        Array<Ref<Expr> > exprs;
        
        while (true)
        {
            Ref<Expr> expr = Statement();
            exprs.Add(expr);
            
            if (!Match(TOKEN_SEMICOLON)) break;
            
            // There may be a trailing line after the last expression in a
            // block. If we eat the line and then see a closing brace or eof,
            // just stop here.
            if (LookAhead(TOKEN_RIGHT_PAREN)) break;
            if (LookAhead(TOKEN_RIGHT_BRACKET)) break;
            if (LookAhead(TOKEN_RIGHT_BRACE)) break;
            if (LookAhead(TOKEN_EOF)) break;
        }
        
        // If there's just one, don't wrap it in a sequence.
        if (exprs.Count() == 1) return exprs[0];
        
        return Ref<Expr>(new SequenceExpr(exprs));
    }
    
    Ref<Expr> FinchParser::Statement()
    {
        // The grammar is carefully constrained to only allow variables to be
        // declared at the "top level" of a block and not inside nested
        // expressions. This is important in order to have a simple single-pass
        // compiler. Doing so requires that we don't have any temporary (i.e.
        // not local variable) registers in use at the point that we are
        // defining a new local. All that means is that variable declarations
        // (var a = "foo") shouldn't be allowed in the middle of message sends.
        // So the grammar must be careful to disallow this:
        //
        //   foo.bar("baz", var a = "blah")

        if (Match(TOKEN_DEF))
        {
            // TODO(bob): Allow parenthesized expression here too.
            Ref<Token> name = Consume(TOKEN_NAME, "Expect name after 'def'.");

            Consume(TOKEN_LEFT_BRACE, "Expect '{' after definition receiver.");
            DefExpr * def = new DefExpr(name->Text());
            ParseDefines(*def, TOKEN_RIGHT_BRACE);
            return Ref<Expr>(def);
        }

        // Named object.
        else if (LookAhead(TOKEN_OBJ, TOKEN_NAME))
        {
            Consume(); // "obj".
            String name = Consume()->Text();

            Ref<Expr> parent;
            if (Match(TOKEN_ARROW))
            {
                parent = Primary();
            }
            else
            {
                parent = Ref<Expr>(new NameExpr("Object"));
            }
            
            ObjectExpr * object = new ObjectExpr(parent);

            Consume(TOKEN_LEFT_BRACE, "Expect '{' after 'obj'.");

            if (!Match(TOKEN_RIGHT_BRACE))
            {
                ParseDefines(*object, TOKEN_RIGHT_BRACE);
            }

            return Ref<Expr>(new VarExpr(name, Ref<Expr>(object)));
        }
        
        if (Match(TOKEN_VAR))
        {
            Ref<Token> name = Consume(TOKEN_NAME, "Expect name after 'var'.");
            // TODO(bob): Handle missing name.
            
            Consume(TOKEN_EQ, "Expect '=' after variable name.");
            
            // handle assigning the special "undefined" value
            if (Match(TOKEN_UNDEFINED))
            {
                return Ref<Expr>(new UndefineExpr(name->Text()));
            }
            else
            {
                Ref<Expr> value = Assignment();
                return Ref<Expr>(new VarExpr(name->Text(), value));
            }
        }

        if (Match(TOKEN_RETURN))
        {
            Ref<Expr> result;
            if (LookAhead(TOKEN_SEMICOLON) ||
                LookAhead(TOKEN_RIGHT_PAREN) ||
                LookAhead(TOKEN_RIGHT_BRACE) ||
                LookAhead(TOKEN_RIGHT_BRACKET))
            {
                // No return value so implicitly return Nil.
                result = Ref<Expr>(new NameExpr("nil"));
            }
            else
            {
                result = Assignment();
            }
            
            return Ref<Expr>(new ReturnExpr(result));
        }

        return Assignment();
    }
        
    Ref<Expr> FinchParser::Assignment()
    {
        if (LookAhead(TOKEN_NAME, TOKEN_EQ))
        {
            String name = Consume()->Text();
            
            Consume(); // "=".
            
            // The initial value.
            Ref<Expr> value = Assignment();
            
            return Ref<Expr>(new SetExpr(name, value));
        }
        else return Operator();
    }
    
    Ref<Expr> FinchParser::Operator()
    {
        Ref<Expr> object = Message();
        
        while (LookAhead(TOKEN_OPERATOR))
        {
            String op = Consume()->Text();
            Ref<Expr> arg = Message();

            Array<Ref<Expr> > args;
            args.Add(arg);
            
            object = Ref<Expr>(new MessageExpr(object, op, args));
        }
        
        return object;
    }

    Ref<Expr> FinchParser::Message()
    {
        Ref<Expr> object;
        if (LookAhead(TOKEN_NAME, TOKEN_LEFT_PAREN) ||
            LookAhead(TOKEN_NAME, TOKEN_LEFT_BRACE))
        {
            // It's a message send to Ether.
            object = ParseMessage(Ref<Expr>(new NameExpr("Ether")));
        }
        else
        {
            object = Primary();
        }

        while (Match(TOKEN_DOT))
        {
            String name;
            Array<Ref<Expr> > args;

            if (LookAhead(TOKEN_NAME, TOKEN_LEFT_PAREN) ||
                LookAhead(TOKEN_NAME, TOKEN_LEFT_BRACE))
            {
                // It's a message send with arguments.
                object = ParseMessage(object);
            }
            else
            {
                // It's an unary message.
                name = Consume(TOKEN_NAME, "Expect message name after '.'")->Text();
                object = Ref<Expr>(new MessageExpr(object, name, args));
            }
        }

        return object;
    }
    
    Ref<Expr> FinchParser::Primary()
    {
        if (LookAhead(TOKEN_NAME))
        {
            String name = Consume()->Text();
            return Ref<Expr>(new NameExpr(name));
        }
        else if (LookAhead(TOKEN_NUMBER))
        {
            return Ref<Expr>(new NumberExpr(Consume()->Number()));
        }
        else if (LookAhead(TOKEN_STRING))
        {
            return Ref<Expr>(new StringExpr(Consume()->Text()));
        }
        else if (Match(TOKEN_SELF))
        {
            return Ref<Expr>(new SelfExpr());
        }
        else if (Match(TOKEN_LEFT_PAREN))
        {
            // Parenthesized expression.
            Ref<Expr> expr = Assignment();
            Consume(TOKEN_RIGHT_PAREN, "Expect closing ')'.");
            return expr;
        }
        else if (Match(TOKEN_OBJ))
        {
            // Object literal.

            Ref<Expr> parent;
            if (Match(TOKEN_ARROW))
            {
                parent = Primary();
            }
            else
            {
                parent = Ref<Expr>(new NameExpr("Object"));
            }

            ObjectExpr * object = new ObjectExpr(parent);
            Ref<Expr> expr = Ref<Expr>(object);

            Consume(TOKEN_LEFT_BRACE, "Expect '{' after 'obj'.");

            if (!Match(TOKEN_RIGHT_BRACE))
            {
                ParseDefines(*object, TOKEN_RIGHT_BRACE);
            }

            return expr;
        }
        else if (Match(TOKEN_LEFT_BRACKET))
        {
            Array<Ref<Expr> > exprs;
            
            // Allow zero-element arrays.
            if (!LookAhead(TOKEN_RIGHT_BRACKET))
            {
                do
                {
                    exprs.Add(Assignment());
                }
                while (Match(TOKEN_COMMA));
            }
            
            Consume(TOKEN_RIGHT_BRACKET, "Expect closing ']'.");
            
            return Ref<Expr>(new ArrayExpr(exprs));
        }
        else if (Match(TOKEN_LEFT_BRACE))
        {
            return ParseBlock();
        }
        else
        {
            Error("Unexpected token.");
            
            // Return some arbitrary expression so that the parser can try to
            // continue and report other errors.
            return Ref<Expr>(new StringExpr("ERROR"));
        }
    }

    Ref<Expr> FinchParser::ParseMessage(Ref<Expr> receiver)
    {
        String name;
        Array<Ref<Expr> > args;

        do
        {
            name += Consume()->Text();

            if (Match(TOKEN_LEFT_BRACE))
            {
                args.Add(ParseBlock());
                name += " ";
            }
            else
            {
                // Parenthesized argument list.
                Consume(TOKEN_LEFT_PAREN, "Expect '(' after method name.");

                // Parse a comma-separated list of arguments.
                do
                {
                    args.Add(Assignment());
                    name += " ";
                }
                while (Match(TOKEN_COMMA));

                Consume(TOKEN_RIGHT_PAREN, "Expect ')' after argument.");
            }
        }
        while (LookAhead(TOKEN_NAME, TOKEN_LEFT_PAREN) ||
               LookAhead(TOKEN_NAME, TOKEN_LEFT_BRACE));

        return Ref<Expr>(new MessageExpr(receiver, name, args));
    }

    Ref<Expr> FinchParser::ParseBlock()
    {
        Array<String> params;

        // Try to parse an argument list. Look for a series of names
        // followed by a "->".
        int numArgs = 0;
        while (LookAhead(numArgs, TOKEN_NAME))
        {
            numArgs++;
        }

        if (numArgs > 0 && LookAhead(numArgs, TOKEN_ARROW))
        {
            for (int i = 0; i < numArgs; i++)
            {
                params.Add(Consume()->Text());
            }

            Consume(); // "->".
        }

        Ref<Expr> body = Expression();
        Consume(TOKEN_RIGHT_BRACE, "Expect closing '}' after block.");

        return Ref<Expr>(new BlockExpr(params, body));
    }

    void FinchParser::ParseDefines(DefineExpr & expr, TokenType endToken)
    {
        while (true)
        {
            ParseDefine(expr);
            if (Match(endToken)) break;
            Consume(TOKEN_SEMICOLON, "Definitions should be separated by newlines (or ';').");
            if (Match(endToken)) break;
        }
    }
    
    void FinchParser::ParseDefine(DefineExpr & expr)
    {
        Array<String> params;
        
        // figure out what kind of thing we're defining
        if (LookAhead(TOKEN_NAME, TOKEN_EQ))
        {
            // object variable
            String name = Consume()->Text();
            Consume(); // <-

            Ref<Expr> body = Assignment();
            
            // if the name is an object variable like "_foo" then the definition
            // just creates that. if it's a local name like "foo" then we will
            // automatically define "_foo" and a method "foo" to access it.
            if (!Expr::IsField(name)) {
                // create the field name
                String varName = String("_") + name;
                
                // define the accessor method
                Ref<Expr> accessor = Ref<Expr>(new NameExpr(varName));
                Ref<Expr> block = Ref<Expr>(new BlockExpr(params, accessor));
                
                expr.Define(true, name, block);
                
                name = varName;
            }
            
            expr.Define(false, name, body);
        }
        else if (LookAhead(TOKEN_NAME, TOKEN_LEFT_BRACE))
        {
            // Unary.
            String name = Consume()->Text();

            ParseDefineBody(expr, name, params);
        }
        else if (LookAhead(TOKEN_NAME))
        {
            // Mixfix.
            String name = "";

            while (LookAhead(TOKEN_NAME))
            {
                name += Consume()->Text();

                Consume(TOKEN_LEFT_PAREN, "Expect '(' after method name.");

                // Parse a comma-separated list of parameters.
                do
                {
                    Ref<Token> param = Consume(TOKEN_NAME,
                                               "Expect parameter name after '('.");
                    params.Add(param->Text());
                    name += " ";
                }
                while (Match(TOKEN_COMMA));

                Consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameter.");
            }
            
            ParseDefineBody(expr, name, params);
        }
        else if (LookAhead(TOKEN_OPERATOR))
        {
            // Binary.
            String name = Consume()->Text();
            
            // One parameter.
            Ref<Token> param = Consume(TOKEN_NAME,
                "Expect parameter name after operator in a bind expression.");
            params.Add(param->Text());

            ParseDefineBody(expr, name, params);
        }
        else
        {
            Error("Expect definition.");
        }
    }
    
    void FinchParser::ParseDefineBody(DefineExpr & expr, String name,
                                         const Array<String> & params)
    {
        // Parse the block.
        Consume(TOKEN_LEFT_BRACE, "Expect '{' to begin bound block.");
        Ref<Expr> body = Expression();
        Consume(TOKEN_RIGHT_BRACE, "Expect '}' to close block.");
        
        // Attach the block's arguments.
        Ref<Expr> block = Ref<Expr>(new BlockExpr(params, body));
        expr.Define(true, name, block);
    }
}

