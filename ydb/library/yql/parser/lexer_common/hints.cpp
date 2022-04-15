#include "hints.h"
#include "parse_hints_impl.h"

#include <ydb/library/yql/utils/yql_panic.h>

#include <util/string/escape.h>

namespace NSQLTranslation {

using NYql::TPosition;

TString TSQLHint::ToString() const {
    TString result;
    TStringOutput out(result);
    Out(out);
    return result;
}

void TSQLHint::Out(IOutputStream& o) const {
    o << "\"" << EscapeC(Name) << "\":{";
    for (size_t i = 0; i < Values.size(); ++i) {
        o << "\"" << EscapeC(Values[i]) << "\"";
        if (i + 1 < Values.size()) {
            o << ",";
        }
    }
    o << "}";
}

namespace {

TPosition ExtractPosition(const TParsedToken& token) {
    return TPosition(token.LinePos + 1, token.Line);
}

class TTokenProcessor {
public:
    explicit TTokenProcessor(TSQLHints& hints)
        : Hints(hints)
    {}

    void ProcessToken(TParsedToken&& token) {
        if (token.Name == "EOF") {
            return;
        }
        auto pos = ExtractPosition(token);
        YQL_ENSURE(!PrevNonCommentPos.Defined() || *PrevNonCommentPos < pos, "Tokens positions should increase monotonically");
        if (token.Name == "WS") {
            return;
        }
        if (token.Name != "COMMENT") {
            PrevNonCommentPos = pos;
            return;
        }
        if (!PrevNonCommentPos) {
            // skip leading comments
            return;
        }
        TVector<TSQLHint> currentHints = NDetail::ParseSqlHints(token.Content);
        if (currentHints.empty()) {
            // no hints here
            return;
        }
        auto& target = Hints[*PrevNonCommentPos];
        target.insert(target.end(), currentHints.begin(), currentHints.end());
    }

private:
    TMaybe<TPosition> PrevNonCommentPos;
    TSQLHints& Hints;
};

}

bool CollectSqlHints(ILexer& lexer, const TString& query, const TString& queryName, TSQLHints& hints, NYql::TIssues& issues, size_t maxErrors) {
    TTokenProcessor tp(hints);
    return lexer.Tokenize(query, queryName, [&tp](TParsedToken&& token) { tp.ProcessToken(std::move(token)); }, issues, maxErrors);
}


}
