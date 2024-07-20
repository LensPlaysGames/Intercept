#ifndef GLINT_PARSER_HH
#define GLINT_PARSER_HH

#include <lcc/context.hh>
#include <lcc/diags.hh>
#include <lcc/syntax/lexer.hh>
#include <lcc/utils.hh>
#include <lcc/utils/result.hh>

#include <glint/ast.hh>
#include <glint/lexer.hh>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lcc::glint {
class Parser : public Lexer {
    using Tk = TokenKind;
    using ExprResult = Result<Expr*>;

    std::vector<Scope*> scope_stack{};
    std::unique_ptr<Module> mod{};

    /// The function we’re currently inside of.
    FuncDecl* curr_func;

public:
    static std::unique_ptr<Module> Parse(Context* context, std::string_view source);
    static std::unique_ptr<Module> Parse(Context* context, File& file);

private:
    static constexpr usz PrefixOperatorPrecedence = 10000;

    /// RAII helper for pushing and popping scopes.
    struct ScopeRAII {
        Parser* parser;
        Scope* scope;

        ScopeRAII(Parser* parser_, Scope* parent = nullptr)
            : parser(parser_), scope(new(*parser->mod) Scope(parent ? parent : parser->CurrScope())) {
            parser->scope_stack.push_back(scope);
        }

        ScopeRAII(const ScopeRAII&) = delete;
        ScopeRAII operator=(const ScopeRAII&) = delete;

        ScopeRAII(ScopeRAII&& other) noexcept
            : parser(other.parser), scope(other.scope) {
            other.scope = nullptr;
        }

        ScopeRAII& operator=(ScopeRAII&& other) noexcept {
            if (this == &other) return *this;
            parser = other.parser;
            scope = other.scope;
            other.scope = nullptr;
            return *this;
        }

        ~ScopeRAII() {
            if (scope) parser->scope_stack.pop_back();
        }
    };

    Parser(Context* ctx, File* file) : Lexer(ctx, file) {}
    Parser(Context* ctx, std::string_view source) : Lexer(ctx, source) {}

    /// Check if we’re at one of a set of tokens.
    [[nodiscard]] static bool Is(GlintToken* tk, auto... tks) { return ((tk->kind == tks) or ...); }
    [[nodiscard]] bool At(auto... tks) { return Is(&tok, tks...); }

    /// Check if we’re at the start of an expression.
    bool AtStartOfExpression();

    /// Like At(), but consume the token if it matches.
    bool Consume(auto... tks) {
        if (At(tks...)) {
            NextToken();
            return true;
        }
        return false;
    }

    /// Get the current scope.
    auto CurrScope() -> Scope* { return scope_stack.back(); }

    /// Get the scope to use for declarations.
    auto DeclScope(bool for_local_var = false) -> Scope* {
        /// Local variables always go in the current scope since
        /// the global scope is *never* the current scope.
        if (for_local_var) return CurrScope();

        /// Globals at the top-level go in the global scope.
        return CurrScope() == TopLevelScope() ? GlobalScope() : CurrScope();
    }

    /// Issue an error.
    template <typename... Args>
    Diag Error(Location where, fmt::format_string<Args...> fmt, Args&&... args) {
        return Diag::Error(context, where, fmt, std::forward<Args>(args)...);
    }

    /// Issue a warning.
    template <typename... Args>
    Diag Warning(Location where, fmt::format_string<Args...> fmt, Args&&... args) {
        return Diag::Warning(context, where, fmt, std::forward<Args>(args)...);
    }

    /// Issue an error at the location of the current token.
    using Lexer::Error;

    /// Issue a warning at the location of the current token.
    using Lexer::Warning;

    auto ParseBlock() -> Result<BlockExpr*>;
    auto ParseBlock(ScopeRAII sc) -> Result<BlockExpr*>;
    auto ParseDecl() -> Result<Decl*>;
    auto ParseDeclRest(std::string ident, Location location, bool is_extern) -> Result<Decl*>;
    auto ParseExpr(isz current_precedence = 0, bool single_expression = false) -> ExprResult;
    auto ParseExprInNewScope() -> ExprResult;
    auto ParseForExpr() -> Result<ForExpr*>;
    auto ParseFuncAttrs() -> Result<FuncType::Attributes>;
    auto ParseFuncBody(bool is_extern) -> Result<std::pair<Expr*, Scope*>>;
    auto ParseFuncDecl(std::string name, FuncType* type, bool is_extern) -> Result<FuncDecl*>;
    auto ParseFuncSig(Type* return_type) -> Result<FuncType*>;
    auto ParseIdentExpr() -> Result<Expr*>;
    auto ParseIfExpr() -> Result<IfExpr*>;
    auto ParsePreamble(File* f) -> Result<void>;
    auto ParseStructType() -> Result<StructType*>;
    auto ParseEnumType() -> Result<EnumType*>;
    auto ParseUnionType() -> Result<UnionType*>;
    auto ParseSumType() -> Result<SumType*>;
    void ParseTopLevel();
    auto ParseType(isz current_precedence = 0) -> Result<Type*>;
    auto ParseWhileExpr() -> Result<WhileExpr*>;

    // Synchronise is normally used when a syntax error occurs, and attempts
    // to get to a position that is known to be a good enough place to start
    // parsing from again. For Glint, this means the hard expression separator
    // ';', or the closing brace of a block.
    void Synchronise();

    /// Get the global scope.
    auto GlobalScope() -> Scope* { return scope_stack[0]; }

    /// Get the scope for local top-level variables.
    /// This is different from the global scope, as a Glint source file is
    /// entirely within `main`.
    auto TopLevelScope() -> Scope* { return scope_stack[1]; }

    friend Scope;
    friend ScopeRAII;
    friend Lexer;
};

} // namespace lcc::glint

#endif // GLINT_PARSER_HH
