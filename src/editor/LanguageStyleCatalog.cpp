#include "editor/LanguageStyleCatalog.h"

#include <SciLexer.h>

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

int qInitResources_LiteCodeThemes();

namespace litecode::editor {
namespace {

void ensureThemeResources() {
    static const bool initialized = [] {
        ::qInitResources_LiteCodeThemes();
        return true;
    }();
    Q_UNUSED(initialized);
}

SyntaxRole roleFromName(const QString& name) {
    static const QHash<QString, SyntaxRole> roles{
        {QStringLiteral("foreground"), SyntaxRole::Foreground},
        {QStringLiteral("comment"), SyntaxRole::Comment},
        {QStringLiteral("keyword"), SyntaxRole::Keyword},
        {QStringLiteral("declaration"), SyntaxRole::Declaration},
        {QStringLiteral("number"), SyntaxRole::Number},
        {QStringLiteral("string"), SyntaxRole::String},
        {QStringLiteral("escape"), SyntaxRole::Escape},
        {QStringLiteral("preprocessor"), SyntaxRole::Preprocessor},
        {QStringLiteral("type"), SyntaxRole::Type},
        {QStringLiteral("parameter"), SyntaxRole::Parameter},
        {QStringLiteral("variable"), SyntaxRole::Variable},
        {QStringLiteral("enumMember"), SyntaxRole::EnumMember},
        {QStringLiteral("function"), SyntaxRole::Function},
        {QStringLiteral("macro"), SyntaxRole::Macro},
        {QStringLiteral("markupHeading"), SyntaxRole::MarkupHeading},
        {QStringLiteral("markupBold"), SyntaxRole::MarkupBold},
        {QStringLiteral("markupItalic"), SyntaxRole::MarkupItalic},
        {QStringLiteral("markupQuote"), SyntaxRole::MarkupQuote},
        {QStringLiteral("markupList"), SyntaxRole::MarkupList},
    };
    return roles.value(name, SyntaxRole::Foreground);
}

QJsonObject languageDefinition(const QString& id) {
    ensureThemeResources();
    QFile file(QStringLiteral(":/litecode/themes/languages/") + id + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

struct LoadedStyleRules final {
    bool found{};
    QVector<LexerStyleRule> rules;
};

LoadedStyleRules loadStyleRules(const QString& id, QSet<QString>& loading) {
    if (id.isEmpty() || loading.contains(id))
        return {};
    loading.insert(id);
    const QJsonObject definition = languageDefinition(id);
    if (definition.isEmpty())
        return {};

    const QString parent = definition.value(QStringLiteral("inherits")).toString();
    LoadedStyleRules result =
        parent.isEmpty() ? LoadedStyleRules{true, {}} : loadStyleRules(parent, loading);
    result.found = true;
    const QJsonObject styles = definition.value(QStringLiteral("styles")).toObject();
    for (auto it = styles.begin(); it != styles.end(); ++it) {
        const SyntaxRole role = roleFromName(it.key());
        const QJsonObject attributes = it.value().toObject();
        const QJsonValue idValue =
            attributes.isEmpty() ? it.value() : attributes.value(QStringLiteral("ids"));
        const QJsonArray ids = idValue.isArray() ? idValue.toArray() : QJsonArray{idValue};
        for (const QJsonValue value : ids)
            result.rules.append({value.toInt(), role,
                                 attributes.value(QStringLiteral("bold")).toBool(),
                                 attributes.value(QStringLiteral("italic")).toBool(),
                                 attributes.value(QStringLiteral("underline")).toBool()});
    }
    loading.remove(id);
    return result;
}

void add(QVector<LexerStyleRule>& rules, SyntaxRole role, std::initializer_list<int> styles) {
    for (const int style : styles)
        rules.append({style, role});
}

QVector<LexerStyleRule> cFamilyRules() {
    QVector<LexerStyleRule> rules;
    add(rules, SyntaxRole::Comment,
        {SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC, SCE_C_COMMENTLINEDOC,
         SCE_C_PREPROCESSORCOMMENT, SCE_C_PREPROCESSORCOMMENTDOC, SCE_C_TASKMARKER});
    add(rules, SyntaxRole::Macro, {SCE_C_COMMENTDOCKEYWORD, SCE_C_COMMENTDOCKEYWORDERROR});
    add(rules, SyntaxRole::Number, {SCE_C_NUMBER});
    add(rules, SyntaxRole::Declaration, {SCE_C_WORD});
    add(rules, SyntaxRole::Keyword, {SCE_C_WORD2});
    add(rules, SyntaxRole::String,
        {SCE_C_STRING, SCE_C_STRINGRAW, SCE_C_CHARACTER, SCE_C_USERLITERAL, SCE_C_STRINGEOL,
         SCE_C_VERBATIM, SCE_C_TRIPLEVERBATIM, SCE_C_HASHQUOTEDSTRING, SCE_C_UUID});
    add(rules, SyntaxRole::Escape, {SCE_C_ESCAPESEQUENCE, SCE_C_REGEX});
    add(rules, SyntaxRole::Preprocessor, {SCE_C_PREPROCESSOR});
    add(rules, SyntaxRole::Foreground, {SCE_C_OPERATOR, SCE_C_IDENTIFIER});
    add(rules, SyntaxRole::Type, {SCE_C_GLOBALCLASS});
    return rules;
}

bool isCppFamily(const QString& id) {
    return id == QStringLiteral("c") || id == QStringLiteral("cpp") ||
           id == QStringLiteral("objective-c") || id == QStringLiteral("objective-cpp") ||
           id == QStringLiteral("cuda-cpp") || id == QStringLiteral("glsl") ||
           id == QStringLiteral("hlsl");
}

bool usesCppLexer(const QString& id) {
    return isCppFamily(id) || id.startsWith(QStringLiteral("javascript")) ||
           id.startsWith(QStringLiteral("typescript")) || id == QStringLiteral("java") ||
           id == QStringLiteral("csharp") || id == QStringLiteral("go") ||
           id == QStringLiteral("kotlin") || id == QStringLiteral("scala") ||
           id == QStringLiteral("swift") || id == QStringLiteral("groovy");
}

} // namespace

bool LanguageStyleCatalog::hasDefinition(const QString& id) {
    return !languageDefinition(id).isEmpty();
}

QVector<KeywordSet> LanguageStyleCatalog::keywordSets(const QString& id) {
    if (isCppFamily(id)) {
        return {
            {0, QByteArrayLiteral("break case catch co_await co_return co_yield continue default "
                                  "delete do else for goto if new operator requires return switch "
                                  "throw try using while")},
            {1, QByteArrayLiteral(
                    "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept "
                    "auto bitand bitor bool char char8_t char16_t char32_t class compl concept "
                    "const consteval constexpr constinit const_cast decltype double dynamic_cast "
                    "enum explicit export extern false float friend inline int long mutable "
                    "namespace noexcept not not_eq nullptr or or_eq private protected public "
                    "register reinterpret_cast short signed sizeof static static_assert "
                    "static_cast struct template this thread_local true typedef typeid typename "
                    "union unsigned virtual void volatile wchar_t xor xor_eq")},
            {2,
             QByteArrayLiteral(
                 "a addindex addtogroup anchor arg attention author authors brief bug callgraph "
                 "callergraph category cite class code cond copybrief copydetails copydoc def "
                 "defgroup deprecated details dir dontinclude dot dotfile e else elseif em "
                 "endcode endcond enddot endif endlink endverbatim enum example exception extends "
                 "file fn headerfile hideinitializer if ifnot image implements include "
                 "includelineno ingroup interface internal invariant li line link mainpage "
                 "memberof "
                 "msc mscfile n namespace name nosubgrouping note overload p package page par "
                 "paragraph param post pre private privatesection property protected "
                 "protectedsection protocol public publicsection pure ref relates relatesalso "
                 "remark remarks result return returns retval sa section see short showinitializer "
                 "since skip skipline snippet struct subpage subsection subsubsection test throw "
                 "throws todo typedef union until var verbatim verbinclude version warning "
                 "weakgroup")},
            {3,
             QByteArrayLiteral(
                 "any array atomic basic_string basic_string_view bitset deque duration function "
                 "future initializer_list iterator list map mutex optional pair path queue set "
                 "shared_ptr span stack string string_view thread tuple unique_ptr unordered_map "
                 "unordered_set variant vector weak_ptr QApplication QAbstractItemModel "
                 "QAbstractScrollArea QAction QByteArray QCheckBox QClipboard QColor QComboBox "
                 "QCoreApplication QDateTime QDialog QDir QDockWidget QElapsedTimer QFile "
                 "QFileInfo "
                 "QFileSystemModel QFont QFrame QFuture QFutureWatcher QGroupBox QIcon QImage "
                 "QLabel "
                 "QLineEdit QListView QLoggingCategory QMainWindow QMenu QMenuBar QMessageBox "
                 "QObject "
                 "QPainter QPalette QPixmap QPlainTextEdit QPoint QProcess QPushButton "
                 "QRadioButton "
                 "QRect QRegularExpression QScrollBar QSettings QSize QSplitter QString "
                 "QStringList "
                 "QStatusBar QTabBar QTabWidget QTableView QTextEdit QThread QTimer QToolBar "
                 "QToolButton QTreeView QUrl QVariant QWidget")},
            {5, QByteArrayLiteral("BUG FIXME HACK NOTE TODO XXX")},
        };
    }
    if (id.startsWith(QStringLiteral("javascript"))) {
        return {
            {0, QByteArrayLiteral(
                    "as async await break case catch class const continue debugger default delete "
                    "do else export extends finally for from function get if import in instanceof "
                    "let new of return set static super switch this throw try typeof var while "
                    "with yield")},
            {1, QByteArrayLiteral("false implements interface null package private protected "
                                  "public true undefined")}};
    }
    if (id.startsWith(QStringLiteral("typescript"))) {
        return {
            {0, QByteArrayLiteral(
                    "as async await break case catch class const continue debugger default delete "
                    "do else export extends finally for from function get if import in instanceof "
                    "let new of return set static super switch this throw try typeof var while "
                    "with yield")},
            {1,
             QByteArrayLiteral(
                 "abstract any asserts bigint boolean declare enum false implements infer "
                 "interface is keyof namespace never null number object private protected public "
                 "readonly string symbol true type undefined unique unknown void")}};
    }
    if (id == QStringLiteral("java")) {
        return {
            {0,
             QByteArrayLiteral(
                 "assert break case catch class continue default do else enum extends finally for "
                 "if implements import instanceof interface new package return switch throw throws "
                 "try while")},
            {1,
             QByteArrayLiteral(
                 "abstract boolean byte char const double false final float goto int long native "
                 "null private protected public short static strictfp super synchronized this "
                 "transient true void volatile")}};
    }
    if (id == QStringLiteral("csharp")) {
        return {
            {0, QByteArrayLiteral(
                    "as async await break case catch class continue default delegate do else enum "
                    "event explicit extern finally fixed for foreach goto if implicit in interface "
                    "is lock namespace new operator out override params ref return struct switch "
                    "throw try unchecked unsafe using virtual while")},
            {1,
             QByteArrayLiteral(
                 "abstract base bool byte char const decimal double false float int internal long "
                 "null object private protected public readonly sbyte sealed short sizeof "
                 "stackalloc "
                 "static string this true typeof uint ulong ushort void volatile")}};
    }
    if (id == QStringLiteral("go")) {
        return {
            {0,
             QByteArrayLiteral(
                 "break case chan const continue default defer else fallthrough for func go goto "
                 "if import interface map package range return select struct switch type var")},
            {1,
             QByteArrayLiteral(
                 "bool byte complex64 complex128 error false float32 float64 int int8 int16 int32 "
                 "int64 iota nil rune string true uint uint8 uint16 uint32 uint64 uintptr")}};
    }
    if (id == QStringLiteral("python")) {
        return {
            {0,
             QByteArrayLiteral(
                 "and as assert async await break class continue def del elif else except False "
                 "finally for from global if import in is lambda None nonlocal not or pass raise "
                 "return True try while with yield")},
            {1,
             QByteArrayLiteral(
                 "abs all any ascii bin bool breakpoint bytearray bytes callable chr classmethod "
                 "compile complex delattr dict dir divmod enumerate eval exec filter float format "
                 "frozenset getattr globals hasattr hash help hex id input int isinstance "
                 "issubclass iter len list locals map max memoryview min next object oct open ord "
                 "pow print property range repr reversed round set setattr slice sorted "
                 "staticmethod str sum super tuple type vars zip __import__")},
        };
    }
    if (id == QStringLiteral("json") || id == QStringLiteral("jsonc") ||
        id == QStringLiteral("jsonl") || id == QStringLiteral("snippets")) {
        return {
            {0, QByteArrayLiteral("false null true")},
            {1, QByteArrayLiteral(
                    "@base @container @context @direction @graph @id @import @included "
                    "@index @json @language @list @nest @none @prefix @propagate @protected "
                    "@reverse @set @type @value @version @vocab")},
        };
    }
    if (id == QStringLiteral("html") || id == QStringLiteral("xml") ||
        id == QStringLiteral("php") || id == QStringLiteral("razor") ||
        id == QStringLiteral("vue") || id == QStringLiteral("svelte") ||
        id == QStringLiteral("astro") || id == QStringLiteral("handlebars")) {
        return {
            {0,
             QByteArrayLiteral(
                 "a abbr address area article aside audio b base bdi bdo blockquote body br button "
                 "canvas caption cite code col colgroup data datalist dd del details dfn dialog "
                 "div dl "
                 "dt em embed fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 head header "
                 "hgroup hr html i iframe img input ins kbd label legend li link main map mark "
                 "menu meta "
                 "meter nav noscript object ol optgroup option output p picture pre progress q rp "
                 "rt ruby "
                 "s samp script search section select slot small source span strong style sub "
                 "summary sup "
                 "table tbody td template textarea tfoot th thead time title tr track u ul var "
                 "video wbr "
                 "accept action alt async autocomplete autofocus charset checked class content "
                 "defer "
                 "disabled download for height hidden href id lang loading max maxlength method "
                 "min multiple "
                 "name pattern placeholder readonly rel required role rows selected src step style "
                 "tabindex "
                 "target title type value width")},
            {1, QByteArrayLiteral("as async await break case catch class const continue debugger "
                                  "default delete do else "
                                  "export extends finally for from function get if import in "
                                  "instanceof let new of return "
                                  "set static super switch this throw try typeof var while with "
                                  "yield false null true undefined")},
            {2, QByteArrayLiteral("and as boolean byref byval call case class const dim do each "
                                  "else elseif end enum event "
                                  "exit false for function get goto if implements in integer is "
                                  "let loop long me new next "
                                  "not nothing object on option or private property public return "
                                  "select set string sub then "
                                  "to true type until variant while with xor")},
            {3, QByteArrayLiteral("and as assert async await break class continue def del elif "
                                  "else except False finally "
                                  "for from global if import in is lambda None nonlocal not or "
                                  "pass raise return True try "
                                  "while with yield")},
            {4, QByteArrayLiteral("__CLASS__ __DIR__ __FILE__ __FUNCTION__ __LINE__ __METHOD__ "
                                  "__NAMESPACE__ and array "
                                  "as break callable case catch class clone const continue declare "
                                  "default die do echo else "
                                  "elseif empty enddeclare endfor endforeach endif endswitch "
                                  "endwhile eval exit extends final "
                                  "finally fn for foreach function global goto if implements "
                                  "include include_once instanceof "
                                  "insteadof interface isset list match namespace new or print "
                                  "private protected public readonly "
                                  "require require_once return static switch throw trait try unset "
                                  "use var while xor yield")},
            {5,
             QByteArrayLiteral(
                 "ATTLIST CDATA DOCTYPE ELEMENT EMPTY ENTITY ID IDREF IDREFS IGNORE INCLUDE NDATA "
                 "NOTATION PCDATA PUBLIC SYSTEM")},
        };
    }
    if (id == QStringLiteral("css") || id == QStringLiteral("scss") ||
        id == QStringLiteral("less")) {
        return {
            {0, QByteArrayLiteral("background background-attachment background-color "
                                  "background-image background-position "
                                  "background-repeat border border-bottom border-collapse "
                                  "border-color border-left border-radius "
                                  "border-right border-spacing border-style border-top "
                                  "border-width bottom box-shadow box-sizing "
                                  "clear color content cursor display fill filter flex flex-basis "
                                  "flex-direction flex-flow "
                                  "flex-grow flex-shrink flex-wrap float font font-family "
                                  "font-size font-style font-weight gap "
                                  "grid grid-area grid-column grid-row height justify-content left "
                                  "letter-spacing line-height "
                                  "list-style margin margin-bottom margin-left margin-right "
                                  "margin-top max-height max-width min-height "
                                  "min-width object-fit opacity order outline overflow overflow-x "
                                  "overflow-y padding padding-bottom "
                                  "padding-left padding-right padding-top position right "
                                  "text-align text-decoration text-overflow "
                                  "text-transform top transform transition vertical-align "
                                  "visibility white-space width z-index")},
            {1, QByteArrayLiteral(
                    "active any-link checked default defined disabled empty enabled first "
                    "first-child first-of-type "
                    "focus focus-visible focus-within hover in-range invalid lang last-child "
                    "last-of-type link "
                    "not nth-child nth-last-child nth-last-of-type nth-of-type only-child "
                    "only-of-type optional "
                    "out-of-range read-only read-write required root target valid visited")},
            {2, QByteArrayLiteral("animation appearance backface-visibility background-clip "
                                  "background-origin background-size "
                                  "border-image border-radius box-decoration-break box-shadow "
                                  "columns column-count column-gap "
                                  "column-rule column-span column-width hyphens resize tab-size "
                                  "text-shadow transform transition")},
            {3, QByteArrayLiteral("align-content align-items align-self animation-delay "
                                  "animation-direction animation-duration "
                                  "animation-fill-mode animation-iteration-count animation-name "
                                  "animation-play-state animation-timing-function "
                                  "aspect-ratio backdrop-filter block-size caret-color clip-path "
                                  "contain container grid-auto-columns "
                                  "grid-auto-flow grid-auto-rows grid-template grid-template-areas "
                                  "grid-template-columns "
                                  "grid-template-rows inline-size inset isolation mask "
                                  "max-block-size max-inline-size min-block-size "
                                  "min-inline-size mix-blend-mode overscroll-behavior "
                                  "place-content place-items place-self pointer-events")},
            {4, QByteArrayLiteral("after backdrop before cue cue-region first-letter first-line "
                                  "file-selector-button marker "
                                  "placeholder selection")},
        };
    }
    if (id == QStringLiteral("groovy")) {
        return {
            {0,
             QByteArrayLiteral("as assert break case catch class continue def default do else "
                               "enum extends finally for goto if implements import in instanceof "
                               "interface new package return switch throw throws trait try while")},
            {1, QByteArrayLiteral("abstract boolean byte char const double false final float int "
                                  "long native null private protected public short static strictfp "
                                  "super synchronized this transient true void volatile")},
        };
    }
    if (id == QStringLiteral("kotlin")) {
        return {{0, QByteArrayLiteral("as break class continue do else false for fun if in "
                                      "interface is null object package return super this throw "
                                      "true try typealias typeof val var when while")}};
    }
    if (id == QStringLiteral("scala")) {
        return {{0, QByteArrayLiteral(
                        "abstract case catch class def do else extends false final "
                        "finally for forSome if implicit import lazy match new null "
                        "object override package private protected return sealed "
                        "super this throw trait try true type val var while with yield")}};
    }
    if (id == QStringLiteral("swift")) {
        return {
            {0, QByteArrayLiteral("as associatedtype break case catch class continue default "
                                  "defer deinit do else enum extension fallthrough false file "
                                  "for func guard if import in init inout internal is let nil "
                                  "open operator private protocol public repeat rethrows return "
                                  "self static struct subscript super switch throw throws true "
                                  "try typealias var where while")}};
    }
    if (id == QStringLiteral("lua")) {
        return {
            {0, QByteArrayLiteral("and break do else elseif end false for function goto if in "
                                  "local nil not or repeat return then true until while")},
            {1,
             QByteArrayLiteral(
                 "assert collectgarbage dofile error getmetatable ipairs load loadfile next pairs "
                 "pcall print rawequal rawget rawlen rawset require select setmetatable tonumber "
                 "tostring type warn xpcall")},
            {2,
             QByteArrayLiteral(
                 "string.byte string.char string.dump string.find string.format string.gmatch "
                 "string.gsub string.len string.lower string.match string.pack string.packsize "
                 "string.rep string.reverse string.sub string.unpack string.upper table.concat "
                 "table.insert table.move table.pack table.remove table.sort table.unpack math.abs "
                 "math.acos math.asin math.atan math.ceil math.cos math.deg math.exp math.floor "
                 "math.fmod math.huge math.log math.max math.maxinteger math.min math.mininteger "
                 "math.modf math.pi math.rad math.random math.randomseed math.sin math.sqrt "
                 "math.tan math.tointeger math.type math.ult")},
            {3, QByteArrayLiteral(
                    "coroutine.close coroutine.create coroutine.isyieldable coroutine.resume "
                    "coroutine.running coroutine.status coroutine.wrap coroutine.yield io.close "
                    "io.flush io.input io.lines io.open io.output io.popen io.read io.tmpfile "
                    "io.type io.write os.clock os.date os.difftime os.execute os.exit os.getenv "
                    "os.remove os.rename os.setlocale os.time os.tmpname")},
        };
    }
    if (id == QStringLiteral("ruby")) {
        return {
            {0, QByteArrayLiteral("BEGIN END alias and begin break case class def defined do "
                                  "else elsif end ensure false for if in module next nil not or "
                                  "redo rescue retry return self super then true undef unless "
                                  "until when while yield")}};
    }
    if (id == QStringLiteral("dart")) {
        return {
            {0, QByteArrayLiteral("abstract as assert async await break case catch class const "
                                  "continue covariant default deferred do dynamic else enum "
                                  "export extends extension external factory false final finally "
                                  "for Function get hide if implements import in interface is "
                                  "late library mixin new null on operator part required rethrow "
                                  "return set show static super switch sync this throw true try "
                                  "typedef var void while with yield")},
            {1, QByteArrayLiteral("false null true")},
            {2, QByteArrayLiteral("async hide of on show sync")},
            {3, QByteArrayLiteral("BigInt bool DateTime double Duration dynamic Enum Function "
                                  "Future int Iterable List Map "
                                  "Never num Object Pattern Record RegExp RuneIterator Set String "
                                  "StringBuffer StringSink "
                                  "Symbol Type Uri void")},
        };
    }
    if (id == QStringLiteral("r")) {
        return {
            {0, QByteArrayLiteral("break else FALSE for function if Inf NA NaN next NULL repeat "
                                  "return TRUE while")},
            {1, QByteArrayLiteral("abs all any apply as.call as.character as.data.frame as.double "
                                  "as.integer as.list "
                                  "as.logical as.matrix as.name as.numeric as.vector attr "
                                  "attributes c call cat ceiling "
                                  "class colnames data.frame dim dimnames do.call duplicated exp "
                                  "floor get grep grepl is.array "
                                  "is.character is.data.frame is.double is.factor is.function "
                                  "is.integer is.list is.logical "
                                  "is.matrix is.na is.null is.numeric lapply length levels list "
                                  "log match matrix max mean "
                                  "min names ncol nrow order paste print prod range rep return rev "
                                  "rnorm round rownames sample "
                                  "sapply seq sort split sqrt stop str sub subset sum summary "
                                  "table tapply unique unlist which")},
            {2, QByteArrayLiteral("aes filter ggplot lm mutate pivot_longer pivot_wider read_csv "
                                  "read_excel rename select "
                                  "summarise tibble transmute write_csv")},
        };
    }
    if (id == QStringLiteral("rust")) {
        return {
            {0,
             QByteArrayLiteral(
                 "as async await break const continue crate dyn else enum extern false fn for if "
                 "impl in let loop match mod move mut pub ref return self Self static struct super "
                 "trait true type union unsafe use where while")},
            {1, QByteArrayLiteral(
                    "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize")},
            {2, QByteArrayLiteral("abstract become box do final macro override priv try typeof "
                                  "unsized virtual yield")},
        };
    }
    if (id == QStringLiteral("shellscript") || id == QStringLiteral("dockerfile")) {
        return {{0, QByteArrayLiteral("break case continue coproc do done elif else esac exit "
                                      "export fi for function if in local readonly return select "
                                      "then time trap typeset until while")}};
    }
    if (id == QStringLiteral("sql")) {
        return {{0, QByteArrayLiteral(
                        "add all alter and any as asc authorization backup begin between by case "
                        "check column commit constraint create cross database default delete desc "
                        "distinct drop else end escape except exists foreign from full grant group "
                        "having in index inner insert intersect into is join key left like limit "
                        "not null offset on or order outer primary procedure references right "
                        "rollback row select set table top transaction trigger union unique update "
                        "use values view when where with")}};
    }
    if (id == QStringLiteral("cmake")) {
        return {
            {0, QByteArrayLiteral(
                    "add_compile_definitions add_compile_options add_custom_command "
                    "add_custom_target add_definitions add_executable add_library add_link_options "
                    "add_subdirectory add_test cmake_minimum_required configure_file elseif else "
                    "enable_testing endforeach endfunction endif endmacro endwhile file find_file "
                    "find_library find_package find_path find_program foreach function "
                    "get_filename_component get_property if include install link_directories list "
                    "macro mark_as_advanced math message option project return separate_arguments "
                    "set set_property string target_compile_definitions target_compile_features "
                    "target_compile_options target_include_directories target_link_directories "
                    "target_link_libraries target_link_options target_sources try_compile unset "
                    "while")},
            {1,
             QByteArrayLiteral(
                 "ALIAS ALL APPEND BEFORE CACHE COMMAND CONFIGURE_DEPENDS DESTINATION "
                 "EXCLUDE_FROM_ALL EXPORT FILES GLOB GLOB_RECURSE IMPORTED INTERFACE LINK_PRIVATE "
                 "LINK_PUBLIC MODULE OBJECT OPTIONAL PRIVATE PROGRAMS PUBLIC QUIET REQUIRED "
                 "RUNTIME SHARED STATIC TARGETS")},
        };
    }
    if (id == QStringLiteral("powershell")) {
        return {
            {0, QByteArrayLiteral(
                    "begin break catch class continue data define do dynamicparam else elseif end "
                    "enum exit filter finally for foreach from function hidden if in inlinescript "
                    "parallel param process return sequence switch throw trap try until using var "
                    "while workflow")},
            {1, QByteArrayLiteral(
                    "add-content clear-content clear-item copy-item export-csv foreach-object "
                    "format-list format-table get-childitem get-command get-content get-help "
                    "get-item get-member get-process get-service import-csv invoke-command "
                    "invoke-expression measure-object move-item new-item out-file read-host "
                    "remove-item rename-item select-object set-content set-item sort-object "
                    "start-process stop-process test-path where-object write-debug write-error "
                    "write-host write-output write-verbose write-warning")},
            {2, QByteArrayLiteral(
                    "ac cat cd chdir clc clear cls copy cp cpi del diff dir echo epal erase exsn "
                    "fc fl "
                    "foreach ft fw gal gc gci gcm gdr ghy gi gl gm gp gps group gsv gu gv history "
                    "icm iex "
                    "ihy ii ipal ipmo irm iselect iwmi kill lp ls man md measure mi mount move mp "
                    "mv nal ndr "
                    "ni nmo npssc nsn nv ogv oh popd ps pushd pwd r rbp rcjb rcsn rd rdr ren ri rm "
                    "rmdir "
                    "rmo rni rnp rp rsn rsnp rv rvpa rwmi sajb sal saps sasv sbp sc select set si "
                    "sl sleep "
                    "sls sort sp spjb spps spsv start sv tee trcm type wget where write")},
            {5,
             QByteArrayLiteral(
                 "example externalhelp forwardhelpkeyname forwardhelptargetname inputs link notes "
                 "outputs "
                 "parameter remotehelprunspace role synopsis component description functionality")},
        };
    }
    if (id == QStringLiteral("yaml") || id == QStringLiteral("dockercompose")) {
        return {
            {0, QByteArrayLiteral("False No Null Off On True Yes false no null off on true yes")}};
    }
    if (id == QStringLiteral("toml")) {
        return {{0, QByteArrayLiteral("false inf nan true")}};
    }
    if (id == QStringLiteral("bat")) {
        return {
            {0, QByteArrayLiteral(
                    "call cd chdir cls color copy date del delete dir echo echo. endlocal "
                    "erase exit for goto if md mkdir mklink move path pause popd prompt "
                    "pushd rd rem ren rename rmdir set setlocal shift start time title type "
                    "ver verify vol")},
            {1, QByteArrayLiteral(
                    "assoc attrib cacls chkdsk choice comp compact convert diskpart "
                    "driverquery fc find findstr format fsutil hostname ipconfig label "
                    "more net ping reg robocopy sc schtasks sort subst systeminfo taskkill "
                    "tasklist where whoami xcopy")},
        };
    }
    if (id == QStringLiteral("asm")) {
        return {
            {0, QByteArrayLiteral(
                    "aaa aad aam aas adc add and bsf bsr bswap bt btc btr bts call cbw cdq "
                    "clc cld cli clts cmc cmp cmps cmpxchg cpuid cwd cwde daa das dec div "
                    "enter hlt idiv imul in inc int into invd invlpg iret iretd jmp lahf lar "
                    "lds lea leave les lfs lgdt lgs lidt lldt lmsw lock lods loop lsl lss ltr "
                    "mov movs movsx movzx mul neg nop not or out outs pop popa popad popf "
                    "popfd push pusha pushad pushf pushfd rcl rcr rdmsr rdpmc rdtsc rep repe "
                    "repne ret rol ror sahf sal sar sbb scas seta setae setb setbe setc sete "
                    "setg setge setl setle setne setno setnp setns seto setp sets sgdt shl "
                    "shld shr shrd sidt sldt smsw stc std sti stos str sub test verr verw wait "
                    "wbinvd wrmsr xadd xchg xlat xor")},
            {1,
             QByteArrayLiteral(
                 "f2xm1 fabs fadd faddp fbstp fchs fclex fcom fcomp fcompp fcos fdecstp "
                 "fdiv fdivp fdivr fdivrp ffree fiadd ficom ficomp fidiv fidivr fild fimul "
                 "fincstp finit fist fistp fisub fisubr fld fld1 fldcw fldenv fldl2e fldl2t "
                 "fldlg2 fldln2 fldpi fldz fmul fmulp fnop fpatan fprem fprem1 fptan frndint "
                 "frstor fscale fsetpm fsin fsincos fsqrt fst fstcw fstenv fstp fsub fsubp "
                 "fsubr fsubrp ftst fucom fucomp fucompp fwait fxam fxch fxtract fyl2x fyl2xp1")},
            {2, QByteArrayLiteral(
                    "ah al ax bh bl bp bx ch cl cs cx dh di dl ds dx eax ebp ebx ecx edi edx "
                    "es esi esp fs gs rax rbp rbx rcx rdi rdx rip rsi rsp ss xmm0 xmm1 xmm2 xmm3 "
                    "xmm4 xmm5 xmm6 xmm7")},
            {3, QByteArrayLiteral(
                    "align bits cpu db dd dq dt dw equ extern global incbin org section struc "
                    "times")},
            {5, QByteArrayLiteral(
                    "addpd addps addsd addss andpd andps blendpd blendps cmppd cmpps cmpsd "
                    "cmpss cvtdq2pd cvtdq2ps cvtpd2dq cvtpd2ps cvtps2dq cvtps2pd cvtsd2si cvtsd2ss "
                    "cvtss2sd divpd divps divsd divss maxpd maxps maxsd maxss minpd minps minsd "
                    "minss movapd movaps movdqa movdqu movsd movss mulpd mulps mulsd mulss orpd "
                    "orps paddb paddd paddq paddw pcmpeqb pcmpeqd psubb psubd psubq psubw shufpd "
                    "shufps sqrtpd sqrtps sqrtsd sqrtss subpd subps subsd subss unpckhpd unpckhps "
                    "unpcklpd unpcklps xorpd xorps")},
        };
    }
    if (id == QStringLiteral("fsharp")) {
        return {
            {0, QByteArrayLiteral(
                    "abstract and as assert base begin class default delegate do done downcast "
                    "downto elif else end exception extern false finally fixed for fun function "
                    "global if in inherit inline interface internal lazy let let! match match! "
                    "member module mutable namespace new null of open or override private public "
                    "rec return return! select static struct then to true try type upcast use use! "
                    "val void when while with yield yield!")},
            {1, QByteArrayLiteral(
                    "abs acos add append asin atan atan2 average averageBy ceil collect compare "
                    "concat contains copy cos count create delay distinct empty exists failwith "
                    "filter find floor fold fst head id ignore init iter length map max min not "
                    "printf printfn raise reduce rev round set sin singleton skip snd sort sprintf "
                    "sqrt sub sum tail toArray toList toSeq trunc tryFind tryHead")},
            {2,
             QByteArrayLiteral(
                 "Array Array2D Array3D Array4D Boolean Byte Char Collections Console DateTime "
                 "Decimal Double Environment Exception Float Int16 Int32 Int64 List Map Math "
                 "Object Option Random Regex Result SByte Seq Set Single String System UInt16 "
                 "UInt32 UInt64 array bigint bool byte char decimal double float int int8 int16 "
                 "int32 int64 list nativeint obj option sbyte seq single string uint uint8 uint16 "
                 "uint32 uint64 unit")},
        };
    }
    if (id == QStringLiteral("coffeescript")) {
        return {
            {0,
             QByteArrayLiteral(
                 "and break by catch class continue delete do else extends finally for from "
                 "function if in instanceof is isnt let loop new not of or own return super switch "
                 "then throw try typeof until when while")},
            {1, QByteArrayLiteral("false null on off true undefined yes no this")},
            {3, QByteArrayLiteral(
                    "Array Boolean Date Error Function JSON Math Number Object RegExp String")},
        };
    }
    if (id == QStringLiteral("julia")) {
        return {
            {0,
             QByteArrayLiteral(
                 "abstract baremodule begin break catch const continue do else elseif end export "
                 "finally for function global if import let local macro module quote return struct "
                 "try using while")},
            {1,
             QByteArrayLiteral(
                 "Any Bool Char Complex Dict Float16 Float32 Float64 Function IO Int Int8 Int16 "
                 "Int32 Int64 Integer Matrix Missing Nothing Number Pair Rational Real Regex Set "
                 "Signed String SubString Symbol Tuple UInt UInt8 UInt16 UInt32 UInt64 Unsigned "
                 "Vector")},
            {2, QByteArrayLiteral("false true missing nothing where primitive mutable type")},
            {3,
             QByteArrayLiteral(
                 "abs all any append! axes basename broadcast cat ceil close collect copy deepcopy "
                 "delete! display eachindex eltype endswith enumerate error exit fill filter "
                 "findall "
                 "first floor get getindex haskey identity include indexin isempty join keys last "
                 "length "
                 "map maximum merge minimum nextfloat open pairs pop! print println push! range "
                 "read "
                 "reduce repeat replace reverse round setdiff show size sort split startswith "
                 "string "
                 "sum typeof values zip")},
        };
    }
    if (id == QStringLiteral("perl")) {
        return {
            {0,
             QByteArrayLiteral(
                 "BEGIN CHECK CORE DESTROY END INIT UNITCHECK __DATA__ __END__ __FILE__ "
                 "__LINE__ __PACKAGE__ abs accept alarm and atan2 bind binmode bless caller "
                 "chdir chmod chomp chop chown chr chroot close closedir cmp connect continue "
                 "cos crypt dbmclose dbmopen defined delete die do dump each else elsif endgrent "
                 "endhostent endnetent endprotoent endpwent endservent eof eq eval exec exists "
                 "exit exp fcntl fileno flock for foreach fork format formline ge getc getgrent "
                 "getgrgid getgrnam gethostbyaddr gethostbyname gethostent getlogin getnetbyaddr "
                 "getnetbyname getnetent getpeername getpgrp getppid getpriority getprotobyname "
                 "getprotobynumber getprotoent getpwent getpwnam getpwuid getservbyname "
                 "getservbyport getservent getsockname getsockopt glob gmtime goto grep gt hex if "
                 "index int join keys kill last lc lcfirst length link listen local localtime lock "
                 "log lstat lt m map mkdir msgctl msgget msgrcv msgsnd my ne next no not oct open "
                 "opendir or ord our pack package pipe pop pos print printf prototype push q qq qr "
                 "quotemeta rand read readdir readline readlink recv redo ref rename require reset "
                 "reverse rewinddir rindex rmdir s scalar seek seekdir select semctl semget semop "
                 "send setgrent sethostent setnetent setpgrp setpriority setprotoent setpwent "
                 "setservent setsockopt shift shmctl shmget shmread shmwrite shutdown sin sleep "
                 "socket "
                 "socketpair sort splice split sprintf sqrt srand stat state study sub substr "
                 "symlink "
                 "syscall sysopen sysread sysseek system syswrite tell telldir tie tied time times "
                 "truncate uc ucfirst umask undef unless unlink unpack unshift untie until use "
                 "utime "
                 "values vec wait waitpid wantarray warn while write xor y")}};
    }
    if (id == QStringLiteral("fortran")) {
        return {
            {0,
             QByteArrayLiteral(
                 "access action advance allocatable allocate assign associate asynchronous "
                 "backspace "
                 "bind blank blockdata call case character class close common complex contains "
                 "continue critical cycle data deallocate default dimension direct do dowhile "
                 "double "
                 "doubleprecision else elseif elsewhere end endassociate enddo endfile endforall "
                 "endfunction endif endinterface endmodule endprogram endselect endsubroutine "
                 "endtype "
                 "endwhere entry equivalence err errmsg exist exit external file flush fmt forall "
                 "form format formatted function go goto id if implicit in include inout integer "
                 "inquire "
                 "intent interface intrinsic iomsg iostat is kind len logical module name namelist "
                 "none nullify only open opened operator optional out pad parameter pass pause "
                 "pointer "
                 "position precision print private procedure program protected public pure read "
                 "real "
                 "recursive result return rewind save select sequence shared sign size stat stop "
                 "submodule "
                 "subroutine target then to type use value volatile wait where while write")},
            {1,
             QByteArrayLiteral(
                 "abs acos adjustl adjustr aimag aint all allocated anint any asin associated atan "
                 "atan2 bit_size btest ceiling char cmplx conjg cos cosh count cpu_time cshift "
                 "date_and_time "
                 "dble digits dim dot_product eoshift epsilon exp exponent floor fraction huge "
                 "iachar "
                 "iand ibclr ibits ibset ichar ieor index int ior ishft ishftc kind lbound len "
                 "len_trim "
                 "log log10 matmul max maxloc maxval merge min minloc minval modulo mvbits nearest "
                 "nint not null pack present product radix random_number range rank reshape "
                 "rrspacing "
                 "scan selected_int_kind shape sign sin sinh size spacing spread sqrt sum "
                 "system_clock "
                 "tan target transfer transpose trim ubound unpack verify")},
            {2,
             QByteArrayLiteral(
                 "cdabs cdcos cdexp cdlog cdsin cdsqrt dcmplx dconjg dcos dcosh ddim dexp dfloat "
                 "dint dlog dlog10 dmax1 dmin1 dmod dnint dprod dreal dsign dsin dsinh dsqrt dtan "
                 "getarg getenv get_command get_environment_variable system")},
        };
    }
    if (id == QStringLiteral("haskell")) {
        return {
            {0,
             QByteArrayLiteral(
                 "case class data default deriving do else hiding if import in infix infixl infixr "
                 "instance let module newtype of then type where forall foreign qualified as")},
            {1,
             QByteArrayLiteral("export label dynamic safe threadsafe unsafe stdcall ccall prim")},
            {2, QByteArrayLiteral(".. : :: = \\ | <- -> @ ~ =>")},
        };
    }
    if (id == QStringLiteral("lisp") || id == QStringLiteral("clojure")) {
        return {
            {0, QByteArrayLiteral(
                    "+ - * / < <= = > >= abs and append apply assoc atom boundp car cdr close cond "
                    "cons defconstant defmacro defparameter defstruct defun defvar eval format "
                    "funcall "
                    "if lambda let let* list load mapcar member nil not null or progn quote remove "
                    "return "
                    "setf setq typep unless when while")},
            {1,
             QByteArrayLiteral(
                 "def defn defmacro defmulti defmethod defonce defprotocol defrecord defstruct fn "
                 "if-let if-not let loop recur when when-let when-not while doseq dotimes future "
                 "go "
                 "ns require use import refer comment true false nil")},
        };
    }
    if (id == QStringLiteral("pascal")) {
        return {
            {0,
             QByteArrayLiteral(
                 "absolute abstract and array as asm assembler automated begin case cdecl class "
                 "const constructor destructor dispid dispinterface div do downto dynamic else end "
                 "except exports external far file final finally for forward function goto helper "
                 "if "
                 "implementation in inherited initialization inline interface is label library "
                 "message "
                 "mod near nil not object of on operator or out overload override packed private "
                 "procedure program property protected public published raise record reference "
                 "register "
                 "repeat resourcestring safecall sealed set shl shr static stdcall strict string "
                 "then "
                 "threadvar to try type unit unsafe until uses var varargs virtual while winapi "
                 "with xor")}};
    }
    if (id == QStringLiteral("vb")) {
        return {
            {0,
             QByteArrayLiteral(
                 "addressof alias and as attribute base begin binary boolean byref byte byval call "
                 "case const currency date decimal declare dim do double each else elseif empty "
                 "end "
                 "enum eqv erase error event exit explicit false for friend function get global "
                 "gosub "
                 "goto if imp implements in input integer is let lib like load lock long loop lset "
                 "me "
                 "mid mod new next not nothing null object on option optional or paramarray "
                 "preserve "
                 "print private property public raiseevent randomize redim rem resume return rset "
                 "seek "
                 "select set single static step stop string sub then time to true type typeof "
                 "unload "
                 "until variant wend while with withevents xor addhandler andalso ansi assembly "
                 "auto "
                 "catch class continue delegate endif finally handles imports inherits interface "
                 "module "
                 "namespace operator overloads overrides readonly shared structure synclock throw "
                 "try using")},
            {1, QByteArrayLiteral(
                    "appactivate beep chdir chdrive close filecopy get input kill line mkdir name "
                    "open "
                    "put reset rmdir savepicture savesetting sendkeys setattr unlock width write")},
        };
    }
    if (id == QStringLiteral("zig")) {
        return {
            {0, QByteArrayLiteral("addrspace align allowzero and anyframe anytype asm async await "
                                  "break callconv catch "
                                  "comptime const continue defer else enum errdefer error export "
                                  "extern false fn for if "
                                  "inline linksection noalias noinline nosuspend null opaque or "
                                  "orelse packed pub resume "
                                  "return struct suspend switch test threadlocal true try "
                                  "undefined union unreachable "
                                  "usingnamespace var volatile while")},
            {1, QByteArrayLiteral("anyerror anyopaque anytype bool c_char c_int c_long "
                                  "c_longdouble c_longlong c_short "
                                  "c_uint c_ulong c_ulonglong c_ushort comptime_float comptime_int "
                                  "error_union f16 f32 "
                                  "f64 f80 f128 i0 i8 i16 i32 i64 i128 isize noreturn type u0 u8 "
                                  "u16 u32 u64 u128 usize "
                                  "void")},
        };
    }
    if (id == QStringLiteral("nim")) {
        return {
            {0,
             QByteArrayLiteral(
                 "addr and as asm bind block break case cast concept const continue converter "
                 "defer discard distinct div do elif else end enum except export finally for from "
                 "func if import in include interface is isnot iterator let macro method mixin mod "
                 "nil "
                 "not notin object of or out proc ptr raise ref return shl shr static template try "
                 "tuple type using var when while xor yield")}};
    }
    if (id == QStringLiteral("d")) {
        return {
            {0, QByteArrayLiteral("abstract alias align asm assert auto bool break byte case cast "
                                  "catch cdouble cent "
                                  "cfloat char class const continue creal dchar debug default "
                                  "delegate delete deprecated "
                                  "do double else enum export extern false final finally float for "
                                  "foreach function goto "
                                  "idouble if ifloat import in inout int interface invariant ireal "
                                  "is lazy long mixin module "
                                  "new null out override package pragma private protected public "
                                  "pure real ref return scope "
                                  "shared short static struct super switch synchronized template "
                                  "this throw true try typedef "
                                  "typeid typeof ubyte uint ulong union unittest ushort version "
                                  "void wchar while with")},
            {2, QByteArrayLiteral("a addindex addtogroup anchor arg author b brief bug c class "
                                  "code date def defgroup "
                                  "deprecated dontinclude e em endcode endif endlink endverbatim "
                                  "enum example exception file "
                                  "fn if image include ingroup internal invariant interface li "
                                  "line link mainpage name namespace "
                                  "note overload p page par param post pre ref relates remarks "
                                  "return retval sa section see "
                                  "since skip struct subsection test throw todo typedef union "
                                  "until var verbatim version warning")},
            {3, QByteArrayLiteral("Object Throwable Exception Error TypeInfo string wstring "
                                  "dstring size_t ptrdiff_t "
                                  "byte ubyte short ushort int uint long ulong cent cfloat cdouble "
                                  "ifloat idouble ireal creal")},
        };
    }
    if (id == QStringLiteral("tcl")) {
        return {
            {0,
             QByteArrayLiteral(
                 "after append array auto_execok auto_import auto_load bgerror binary break case "
                 "catch cd clock close concat continue dde default echo else elseif encoding eof "
                 "error "
                 "eval exec exit expr fblocked fconfigure fcopy file fileevent flush for foreach "
                 "format "
                 "gets glob global history http if incr info interp join lappend lindex linsert "
                 "list "
                 "llength load lrange lreplace lsearch lset lsort namespace open package pid proc "
                 "puts "
                 "pwd read regexp registry regsub rename return scan seek set socket source split "
                 "string "
                 "subst switch tell time trace unknown unset update uplevel upvar variable vwait "
                 "while")},
            {1,
             QByteArrayLiteral(
                 "bell bind bindtags bitmap button canvas checkbutton clipboard colors console "
                 "destroy "
                 "entry event focus font frame grab grid image label labelframe listbox lower menu "
                 "menubutton message option pack panedwindow photo place radiobutton raise scale "
                 "scrollbar "
                 "selection send spinbox text tk tk_chooseColor tk_chooseDirectory tk_dialog "
                 "tk_focusNext "
                 "tk_getOpenFile tk_messageBox tk_optionMenu tk_popup tk_setPalette tkwait "
                 "toplevel "
                 "winfo wish wm")},
            {2, QByteArrayLiteral("body class code common component configbody constructor "
                                  "destructor hull import inherit "
                                  "itcl itk_component itk_initialize itk_interior itk_option "
                                  "iwidgets keep method private "
                                  "protected public")},
            {3,
             QByteArrayLiteral(
                 "tk_bisque tk_chooseColor tk_dialog tk_focusFollowsMouse tk_focusNext "
                 "tk_focusPrev "
                 "tk_getOpenFile tk_getSaveFile tk_messageBox tk_optionMenu tk_popup tk_setPalette "
                 "tk_textCopy tk_textCut tk_textPaste")},
            {4, QByteArrayLiteral("expand")},
        };
    }
    if (id == QStringLiteral("verilog")) {
        return {
            {0,
             QByteArrayLiteral(
                 "always and assign automatic begin buf bufif0 bufif1 case casex casez cell cmos "
                 "config deassign default defparam design disable edge else end endcase endconfig "
                 "endfunction endgenerate endmodule endprimitive endspecify endtable endtask event "
                 "for "
                 "force forever fork function generate genvar highz0 highz1 if ifnone include "
                 "initial "
                 "inout input instance integer join large liblist library localparam macromodule "
                 "medium "
                 "module nand negedge nmos nor not notif0 notif1 or output parameter pmos posedge "
                 "primitive "
                 "pull0 pull1 pulldown pullup pulsestyle_onevent pulsestyle_ondetect rcmos real "
                 "realtime "
                 "reg release repeat rnmos rpmos rtran rtranif0 rtranif1 scalared signed small "
                 "specify "
                 "specparam strong0 strong1 supply0 supply1 table task time tran tranif0 tranif1 "
                 "tri tri0 "
                 "tri1 triand trior trireg unsigned use vectored wait wand weak0 weak1 while wire "
                 "wor xnor xor")},
            {2, QByteArrayLiteral("$bits $bitstoreal $clog2 $countdrivers $display $displayb "
                                  "$displayh $displayo $dist_chi_square "
                                  "$dumpall $dumpfile $dumpflush $dumplimit $dumpoff $dumpon "
                                  "$dumpvars $fclose $fdisplay "
                                  "$feof $ferror $fflush $fgetc $fgets $finish $fopen $fread "
                                  "$fscanf $fseek $fsscanf $fwrite "
                                  "$getpattern $history $hold $itor $key $list $log $monitor "
                                  "$random $readmemh $readmemb "
                                  "$realtime $reset $rtoi $signed $stime $stop $time $timeformat "
                                  "$unsigned $value$plusargs "
                                  "$write")},
            {4, QByteArrayLiteral("TODO infer_mux parallel_case synopsys")},
        };
    }
    if (id == QStringLiteral("vhdl")) {
        return {
            {0, QByteArrayLiteral("access after alias all architecture array assert attribute "
                                  "begin block body buffer bus "
                                  "case component configuration constant disconnect downto else "
                                  "elsif end entity exit file "
                                  "for function generate generic group guarded if impure in "
                                  "inertial inout is label library "
                                  "linkage literal loop map new next null of on open others out "
                                  "package port postponed procedure "
                                  "process pure range record register reject report return select "
                                  "severity shared signal subtype "
                                  "then to transport type unaffected units until use variable wait "
                                  "when while with")},
            {1,
             QByteArrayLiteral("abs and mod nand nor not or rem rol ror sla sll sra srl xnor xor")},
            {2, QByteArrayLiteral(
                    "left right low high ascending image value pos val succ pred leftof rightof "
                    "base range "
                    "reverse_range length delayed stable quiet transaction event active last_event "
                    "last_active "
                    "last_value driving driving_value simple_name path_name instance_name")},
            {3, QByteArrayLiteral("now readline read writeline write endfile resolved to_bit "
                                  "to_bitvector to_stdulogic "
                                  "to_stdlogicvector to_stdulogicvector to_x01 to_x01z to_UX01 "
                                  "rising_edge falling_edge "
                                  "is_x shift_left shift_right rotate_left rotate_right resize "
                                  "to_integer to_unsigned "
                                  "to_signed std_match")},
            {4, QByteArrayLiteral(
                    "std ieee work standard textio std_logic_1164 std_logic_arith std_logic_misc "
                    "std_logic_signed std_logic_textio std_logic_unsigned numeric_bit numeric_std "
                    "math_complex "
                    "math_real vital_primitives vital_timing")},
            {5, QByteArrayLiteral("boolean bit character severity_level integer real time "
                                  "delay_length natural positive "
                                  "string bit_vector file_open_kind file_open_status line text "
                                  "side width std_ulogic "
                                  "std_ulogic_vector std_logic std_logic_vector X01 X01Z UX01 "
                                  "UX01Z unsigned signed")},
        };
    }
    if (id == QStringLiteral("makefile")) {
        return {
            {0,
             QByteArrayLiteral(
                 "define else endef endif export ifdef ifeq ifndef ifneq include override private "
                 "sinclude undefine unexport vpath")}};
    }
    return {};
}

QVector<LexerStyleRule> LanguageStyleCatalog::styleRules(const QString& id) {
    QSet<QString> loading;
    const LoadedStyleRules external = loadStyleRules(id, loading);
    if (external.found)
        return external.rules;

    // Safe fallback for an external or unregistered lexer. Shipped languages use
    // the compact JSON definitions above.
    if (usesCppLexer(id))
        return cFamilyRules();

    QVector<LexerStyleRule> rules;
    if (id == QStringLiteral("python")) {
        add(rules, SyntaxRole::Comment, {SCE_P_COMMENTLINE, SCE_P_COMMENTBLOCK});
        add(rules, SyntaxRole::String,
            {SCE_P_STRING, SCE_P_CHARACTER, SCE_P_TRIPLE, SCE_P_TRIPLEDOUBLE, SCE_P_FSTRING,
             SCE_P_FCHARACTER, SCE_P_FTRIPLE, SCE_P_FTRIPLEDOUBLE});
        add(rules, SyntaxRole::Number, {SCE_P_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_P_WORD});
        add(rules, SyntaxRole::Type, {SCE_P_CLASSNAME});
        add(rules, SyntaxRole::Function, {SCE_P_DEFNAME, SCE_P_WORD2});
        add(rules, SyntaxRole::Macro, {SCE_P_DECORATOR});
    } else if (id == QStringLiteral("json")) {
        add(rules, SyntaxRole::Comment, {SCE_JSON_LINECOMMENT, SCE_JSON_BLOCKCOMMENT});
        add(rules, SyntaxRole::String, {SCE_JSON_STRING});
        add(rules, SyntaxRole::Escape, {SCE_JSON_ESCAPESEQUENCE});
        add(rules, SyntaxRole::Variable, {SCE_JSON_PROPERTYNAME});
        add(rules, SyntaxRole::Number, {SCE_JSON_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_JSON_KEYWORD});
    } else if (id == QStringLiteral("markdown") || id == QStringLiteral("mermaid")) {
        add(rules, SyntaxRole::MarkupHeading,
            {SCE_MARKDOWN_HEADER1, SCE_MARKDOWN_HEADER2, SCE_MARKDOWN_HEADER3, SCE_MARKDOWN_HEADER4,
             SCE_MARKDOWN_HEADER5, SCE_MARKDOWN_HEADER6});
        add(rules, SyntaxRole::MarkupBold, {SCE_MARKDOWN_STRONG1, SCE_MARKDOWN_STRONG2});
        add(rules, SyntaxRole::MarkupQuote,
            {SCE_MARKDOWN_ULIST_ITEM, SCE_MARKDOWN_OLIST_ITEM, SCE_MARKDOWN_BLOCKQUOTE});
        add(rules, SyntaxRole::Type, {SCE_MARKDOWN_LINK});
        add(rules, SyntaxRole::String,
            {SCE_MARKDOWN_CODE, SCE_MARKDOWN_CODE2, SCE_MARKDOWN_CODEBK});
    } else if (id == QStringLiteral("latex")) {
        add(rules, SyntaxRole::Comment, {SCE_L_COMMENT, SCE_L_COMMENT2});
        add(rules, SyntaxRole::Function, {SCE_L_COMMAND, SCE_L_SHORTCMD});
        add(rules, SyntaxRole::Type, {SCE_L_TAG, SCE_L_TAG2});
        add(rules, SyntaxRole::Number, {SCE_L_MATH, SCE_L_MATH2});
        add(rules, SyntaxRole::String, {SCE_L_VERBATIM});
        add(rules, SyntaxRole::Parameter, {SCE_L_CMDOPT});
    } else if (id == QStringLiteral("git-config") || id == QStringLiteral("ini") ||
               id == QStringLiteral("properties")) {
        add(rules, SyntaxRole::Comment, {SCE_PROPS_COMMENT});
        add(rules, SyntaxRole::Type, {SCE_PROPS_SECTION});
        add(rules, SyntaxRole::Variable, {SCE_PROPS_KEY});
        add(rules, SyntaxRole::Foreground, {SCE_PROPS_ASSIGNMENT});
        add(rules, SyntaxRole::String, {SCE_PROPS_DEFVAL});
    } else if (id == QStringLiteral("html") || id == QStringLiteral("xml") ||
               id == QStringLiteral("php") || id == QStringLiteral("razor") ||
               id == QStringLiteral("vue") || id == QStringLiteral("svelte") ||
               id == QStringLiteral("astro")) {
        add(rules, SyntaxRole::Comment, {SCE_H_COMMENT});
        add(rules, SyntaxRole::String, {SCE_H_DOUBLESTRING, SCE_H_SINGLESTRING});
        add(rules, SyntaxRole::Declaration, {SCE_H_TAG});
        add(rules, SyntaxRole::Type, {SCE_H_ATTRIBUTE});
        add(rules, SyntaxRole::Number, {SCE_H_ENTITY});
    } else if (id == QStringLiteral("css") || id == QStringLiteral("scss") ||
               id == QStringLiteral("less")) {
        add(rules, SyntaxRole::Comment, {SCE_CSS_COMMENT});
        add(rules, SyntaxRole::String, {SCE_CSS_DOUBLESTRING, SCE_CSS_SINGLESTRING});
        add(rules, SyntaxRole::Declaration, {SCE_CSS_TAG});
        add(rules, SyntaxRole::Type, {SCE_CSS_CLASS});
        add(rules, SyntaxRole::Variable, {SCE_CSS_IDENTIFIER, SCE_CSS_VARIABLE});
        add(rules, SyntaxRole::String, {SCE_CSS_VALUE});
    } else if (id == QStringLiteral("yaml")) {
        add(rules, SyntaxRole::Comment, {SCE_YAML_COMMENT});
        add(rules, SyntaxRole::Variable, {SCE_YAML_IDENTIFIER});
        add(rules, SyntaxRole::Number, {SCE_YAML_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_YAML_KEYWORD});
    } else if (id == QStringLiteral("powershell")) {
        add(rules, SyntaxRole::Comment, {SCE_POWERSHELL_COMMENT, SCE_POWERSHELL_COMMENTSTREAM});
        add(rules, SyntaxRole::String,
            {SCE_POWERSHELL_STRING, SCE_POWERSHELL_CHARACTER, SCE_POWERSHELL_HERE_STRING,
             SCE_POWERSHELL_HERE_CHARACTER});
        add(rules, SyntaxRole::Keyword, {SCE_POWERSHELL_KEYWORD});
        add(rules, SyntaxRole::Variable, {SCE_POWERSHELL_VARIABLE});
        add(rules, SyntaxRole::Function, {SCE_POWERSHELL_CMDLET});
    } else if (id == QStringLiteral("rust")) {
        add(rules, SyntaxRole::Comment,
            {SCE_RUST_COMMENTBLOCK, SCE_RUST_COMMENTLINE, SCE_RUST_COMMENTBLOCKDOC,
             SCE_RUST_COMMENTLINEDOC});
        add(rules, SyntaxRole::String, {SCE_RUST_STRING, SCE_RUST_STRINGR, SCE_RUST_CHARACTER});
        add(rules, SyntaxRole::Keyword, {SCE_RUST_WORD, SCE_RUST_WORD3});
        add(rules, SyntaxRole::Type, {SCE_RUST_WORD2});
        add(rules, SyntaxRole::Number, {SCE_RUST_NUMBER});
        add(rules, SyntaxRole::Macro, {SCE_RUST_MACRO});
    } else if (id == QStringLiteral("shellscript") || id == QStringLiteral("dockerfile")) {
        add(rules, SyntaxRole::Comment, {SCE_SH_COMMENTLINE});
        add(rules, SyntaxRole::String, {SCE_SH_STRING, SCE_SH_CHARACTER, SCE_SH_BACKTICKS});
        add(rules, SyntaxRole::Keyword, {SCE_SH_WORD});
        add(rules, SyntaxRole::Variable, {SCE_SH_SCALAR});
    } else if (id == QStringLiteral("sql")) {
        add(rules, SyntaxRole::Comment, {SCE_SQL_COMMENT, SCE_SQL_COMMENTLINE, SCE_SQL_COMMENTDOC});
        add(rules, SyntaxRole::String, {SCE_SQL_STRING, SCE_SQL_CHARACTER});
        add(rules, SyntaxRole::Keyword, {SCE_SQL_WORD});
        add(rules, SyntaxRole::Number, {SCE_SQL_NUMBER});
    } else if (id == QStringLiteral("cmake")) {
        add(rules, SyntaxRole::Comment, {SCE_CMAKE_COMMENT});
        add(rules, SyntaxRole::String,
            {SCE_CMAKE_STRINGDQ, SCE_CMAKE_STRINGLQ, SCE_CMAKE_STRINGRQ});
        add(rules, SyntaxRole::Function, {SCE_CMAKE_COMMANDS});
        add(rules, SyntaxRole::Parameter, {SCE_CMAKE_PARAMETERS});
        add(rules, SyntaxRole::Variable, {SCE_CMAKE_VARIABLE});
        add(rules, SyntaxRole::Number, {SCE_CMAKE_NUMBER});
    } else if (id == QStringLiteral("toml")) {
        add(rules, SyntaxRole::Comment, {SCE_TOML_COMMENT});
        add(rules, SyntaxRole::String,
            {SCE_TOML_STRING_SQ, SCE_TOML_STRING_DQ, SCE_TOML_TRIPLE_STRING_SQ,
             SCE_TOML_TRIPLE_STRING_DQ});
        add(rules, SyntaxRole::Variable, {SCE_TOML_KEY});
        add(rules, SyntaxRole::Type, {SCE_TOML_TABLE});
        add(rules, SyntaxRole::Number, {SCE_TOML_NUMBER});
    } else if (id == QStringLiteral("lua")) {
        add(rules, SyntaxRole::Comment, {SCE_LUA_COMMENT, SCE_LUA_COMMENTLINE, SCE_LUA_COMMENTDOC});
        add(rules, SyntaxRole::String, {SCE_LUA_STRING, SCE_LUA_CHARACTER, SCE_LUA_LITERALSTRING});
        add(rules, SyntaxRole::Number, {SCE_LUA_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_LUA_WORD});
        add(rules, SyntaxRole::Function, {SCE_LUA_WORD2, SCE_LUA_WORD3, SCE_LUA_WORD4});
        add(rules, SyntaxRole::Preprocessor, {SCE_LUA_PREPROCESSOR});
    } else if (id == QStringLiteral("ruby")) {
        add(rules, SyntaxRole::Comment, {SCE_RB_COMMENTLINE, SCE_RB_POD});
        add(rules, SyntaxRole::String,
            {SCE_RB_STRING, SCE_RB_CHARACTER, SCE_RB_HERE_Q, SCE_RB_HERE_QQ, SCE_RB_STRING_Q,
             SCE_RB_STRING_QQ});
        add(rules, SyntaxRole::Escape, {SCE_RB_REGEX, SCE_RB_STRING_QR});
        add(rules, SyntaxRole::Number, {SCE_RB_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_RB_WORD, SCE_RB_WORD_DEMOTED});
        add(rules, SyntaxRole::Type, {SCE_RB_CLASSNAME, SCE_RB_MODULE_NAME});
        add(rules, SyntaxRole::Function, {SCE_RB_DEFNAME});
        add(rules, SyntaxRole::Variable, {SCE_RB_GLOBAL, SCE_RB_INSTANCE_VAR, SCE_RB_CLASS_VAR});
    } else if (id == QStringLiteral("perl")) {
        add(rules, SyntaxRole::Comment, {SCE_PL_COMMENTLINE, SCE_PL_POD});
        add(rules, SyntaxRole::String,
            {SCE_PL_STRING, SCE_PL_CHARACTER, SCE_PL_LONGQUOTE, SCE_PL_HERE_Q, SCE_PL_HERE_QQ,
             SCE_PL_STRING_Q, SCE_PL_STRING_QQ});
        add(rules, SyntaxRole::Escape, {SCE_PL_REGEX, SCE_PL_REGSUBST, SCE_PL_STRING_QR});
        add(rules, SyntaxRole::Number, {SCE_PL_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_PL_WORD});
        add(rules, SyntaxRole::Variable, {SCE_PL_SCALAR, SCE_PL_ARRAY, SCE_PL_HASH});
    } else if (id == QStringLiteral("r")) {
        add(rules, SyntaxRole::Comment, {SCE_R_COMMENT});
        add(rules, SyntaxRole::String,
            {SCE_R_STRING, SCE_R_STRING2, SCE_R_RAWSTRING, SCE_R_RAWSTRING2});
        add(rules, SyntaxRole::Escape, {SCE_R_ESCAPESEQUENCE});
        add(rules, SyntaxRole::Number, {SCE_R_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_R_KWORD, SCE_R_BASEKWORD, SCE_R_OTHERKWORD});
        add(rules, SyntaxRole::Variable, {SCE_R_IDENTIFIER});
    } else if (id == QStringLiteral("dart")) {
        add(rules, SyntaxRole::Comment,
            {SCE_DART_COMMENTLINE, SCE_DART_COMMENTLINEDOC, SCE_DART_COMMENTBLOCK,
             SCE_DART_COMMENTBLOCKDOC});
        add(rules, SyntaxRole::String,
            {SCE_DART_STRING_SQ, SCE_DART_STRING_DQ, SCE_DART_TRIPLE_STRING_SQ,
             SCE_DART_TRIPLE_STRING_DQ, SCE_DART_RAWSTRING_SQ, SCE_DART_RAWSTRING_DQ});
        add(rules, SyntaxRole::Escape, {SCE_DART_ESCAPECHAR});
        add(rules, SyntaxRole::Number, {SCE_DART_NUMBER});
        add(rules, SyntaxRole::Keyword,
            {SCE_DART_KW_PRIMARY, SCE_DART_KW_SECONDARY, SCE_DART_KW_TERTIARY});
        add(rules, SyntaxRole::Type, {SCE_DART_KW_TYPE});
        add(rules, SyntaxRole::Macro, {SCE_DART_METADATA});
    } else if (id == QStringLiteral("fsharp")) {
        add(rules, SyntaxRole::Comment, {SCE_FSHARP_COMMENT, SCE_FSHARP_COMMENTLINE});
        add(rules, SyntaxRole::String,
            {SCE_FSHARP_STRING, SCE_FSHARP_CHARACTER, SCE_FSHARP_VERBATIM, SCE_FSHARP_QUOTATION});
        add(rules, SyntaxRole::Number, {SCE_FSHARP_NUMBER});
        add(rules, SyntaxRole::Keyword,
            {SCE_FSHARP_KEYWORD, SCE_FSHARP_KEYWORD2, SCE_FSHARP_KEYWORD3, SCE_FSHARP_KEYWORD4,
             SCE_FSHARP_KEYWORD5});
        add(rules, SyntaxRole::Preprocessor, {SCE_FSHARP_PREPROCESSOR});
        add(rules, SyntaxRole::Macro, {SCE_FSHARP_ATTRIBUTE});
    } else if (id == QStringLiteral("coffeescript")) {
        add(rules, SyntaxRole::Comment,
            {SCE_COFFEESCRIPT_COMMENT, SCE_COFFEESCRIPT_COMMENTLINE, SCE_COFFEESCRIPT_COMMENTDOC,
             SCE_COFFEESCRIPT_COMMENTBLOCK});
        add(rules, SyntaxRole::String,
            {SCE_COFFEESCRIPT_STRING, SCE_COFFEESCRIPT_CHARACTER, SCE_COFFEESCRIPT_VERBATIM,
             SCE_COFFEESCRIPT_STRINGRAW});
        add(rules, SyntaxRole::Escape, {SCE_COFFEESCRIPT_REGEX, SCE_COFFEESCRIPT_VERBOSE_REGEX});
        add(rules, SyntaxRole::Number, {SCE_COFFEESCRIPT_NUMBER});
        add(rules, SyntaxRole::Keyword, {SCE_COFFEESCRIPT_WORD, SCE_COFFEESCRIPT_WORD2});
        add(rules, SyntaxRole::Type, {SCE_COFFEESCRIPT_GLOBALCLASS});
        add(rules, SyntaxRole::Variable, {SCE_COFFEESCRIPT_INSTANCEPROPERTY});
    } else if (id == QStringLiteral("makefile")) {
        add(rules, SyntaxRole::Comment, {SCE_MAKE_COMMENT});
        add(rules, SyntaxRole::Preprocessor, {SCE_MAKE_PREPROCESSOR});
        add(rules, SyntaxRole::Variable, {SCE_MAKE_IDENTIFIER});
        add(rules, SyntaxRole::Function, {SCE_MAKE_TARGET});
    } else if (id == QStringLiteral("diff")) {
        add(rules, SyntaxRole::Comment, {SCE_DIFF_COMMENT});
        add(rules, SyntaxRole::Preprocessor, {SCE_DIFF_COMMAND, SCE_DIFF_HEADER});
        add(rules, SyntaxRole::Type, {SCE_DIFF_POSITION});
        add(rules, SyntaxRole::String,
            {SCE_DIFF_DELETED, SCE_DIFF_PATCH_DELETE, SCE_DIFF_REMOVED_PATCH_DELETE});
        add(rules, SyntaxRole::Number,
            {SCE_DIFF_ADDED, SCE_DIFF_PATCH_ADD, SCE_DIFF_REMOVED_PATCH_ADD});
        add(rules, SyntaxRole::Keyword, {SCE_DIFF_CHANGED});
    } else if (id == QStringLiteral("asm")) {
        add(rules, SyntaxRole::Comment, {SCE_ASM_COMMENT, SCE_ASM_COMMENTBLOCK});
        add(rules, SyntaxRole::String,
            {SCE_ASM_STRING, SCE_ASM_CHARACTER, SCE_ASM_STRINGBACKQUOTE});
        add(rules, SyntaxRole::Number, {SCE_ASM_NUMBER});
        add(rules, SyntaxRole::Keyword,
            {SCE_ASM_CPUINSTRUCTION, SCE_ASM_MATHINSTRUCTION, SCE_ASM_EXTINSTRUCTION});
        add(rules, SyntaxRole::Variable, {SCE_ASM_REGISTER});
        add(rules, SyntaxRole::Preprocessor, {SCE_ASM_DIRECTIVE});
    } else if (id == QStringLiteral("julia")) {
        add(rules, SyntaxRole::Comment, {SCE_JULIA_COMMENT, SCE_JULIA_DOCSTRING});
        add(rules, SyntaxRole::String, {SCE_JULIA_STRING, SCE_JULIA_CHAR, SCE_JULIA_STRINGLITERAL});
        add(rules, SyntaxRole::Number, {SCE_JULIA_NUMBER});
        add(rules, SyntaxRole::Keyword,
            {SCE_JULIA_KEYWORD1, SCE_JULIA_KEYWORD2, SCE_JULIA_KEYWORD3, SCE_JULIA_KEYWORD4});
        add(rules, SyntaxRole::Type, {SCE_JULIA_TYPEANNOT});
        add(rules, SyntaxRole::Macro, {SCE_JULIA_MACRO});
    }
    return rules;
}

SyntaxRole LanguageStyleCatalog::roleForStyleMetadata(const QString& name, const QString& tags,
                                                      const QString& description) {
    const QString metadata =
        (name + QLatin1Char(' ') + tags + QLatin1Char(' ') + description).toLower();
    const auto containsAny = [&metadata](std::initializer_list<QStringView> terms) {
        for (const QStringView term : terms) {
            if (metadata.contains(term))
                return true;
        }
        return false;
    };

    if (containsAny({u"comment", u"documentation"}))
        return SyntaxRole::Comment;
    if (containsAny({u"escape", u"regex", u"regular expression"}))
        return SyntaxRole::Escape;
    if (containsAny({u"string", u"character", u"quoted", u"heredoc", u"verbatim"}))
        return SyntaxRole::String;
    if (containsAny({u"number", u"numeric", u"float", u"integer"}))
        return SyntaxRole::Number;
    if (containsAny({u"preprocessor", u"directive"}))
        return SyntaxRole::Preprocessor;
    if (containsAny({u"function", u"method", u"command", u"target"}))
        return SyntaxRole::Function;
    if (containsAny({u"class", u"type", u"interface", u"structure", u"tag"}))
        return SyntaxRole::Type;
    if (containsAny({u"parameter", u"argument"}))
        return SyntaxRole::Parameter;
    if (containsAny({u"variable", u"property", u"identifier", u"key"}))
        return SyntaxRole::Variable;
    if (containsAny({u"macro", u"decorator", u"attribute"}))
        return SyntaxRole::Macro;
    if (containsAny({u"keyword", u"reserved word", u"word"}))
        return SyntaxRole::Keyword;
    return SyntaxRole::Foreground;
}

} // namespace litecode::editor
