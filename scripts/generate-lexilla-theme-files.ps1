param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$SciLexerHeader = ""
)

$ErrorActionPreference = "Stop"
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path

if (-not $SciLexerHeader) {
    $SciLexerHeader = Get-ChildItem -Path (Join-Path $SourceRoot "build") -Filter SciLexer.h -Recurse |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $SciLexerHeader -or -not (Test-Path -LiteralPath $SciLexerHeader)) {
    throw "SciLexer.h was not found. Configure LiteCode before regenerating theme files."
}

$definitions = @{}
foreach ($line in Get-Content -LiteralPath $SciLexerHeader) {
    if ($line -match '^#define\s+(SCE_[A-Z0-9_]+)\s+([0-9]+)$') {
        $definitions[$matches[1]] = [int]$matches[2]
    }
}

$profiles = [ordered]@{
    cpp = [ordered]@{
        comment = @('SCE_C_COMMENT', 'SCE_C_COMMENTLINE', 'SCE_C_COMMENTDOC', 'SCE_C_COMMENTLINEDOC', 'SCE_C_PREPROCESSORCOMMENT', 'SCE_C_PREPROCESSORCOMMENTDOC', 'SCE_C_TASKMARKER')
        macro = @('SCE_C_COMMENTDOCKEYWORD', 'SCE_C_COMMENTDOCKEYWORDERROR')
        number = @('SCE_C_NUMBER')
        declaration = @('SCE_C_WORD')
        keyword = @('SCE_C_WORD2')
        string = @('SCE_C_STRING', 'SCE_C_STRINGRAW', 'SCE_C_CHARACTER', 'SCE_C_USERLITERAL', 'SCE_C_STRINGEOL', 'SCE_C_VERBATIM', 'SCE_C_TRIPLEVERBATIM', 'SCE_C_HASHQUOTEDSTRING', 'SCE_C_UUID')
        escape = @('SCE_C_ESCAPESEQUENCE', 'SCE_C_REGEX')
        preprocessor = @('SCE_C_PREPROCESSOR')
        foreground = @('SCE_C_OPERATOR', 'SCE_C_IDENTIFIER')
        type = @('SCE_C_GLOBALCLASS')
    }
    python = [ordered]@{ comment=@('SCE_P_COMMENTLINE','SCE_P_COMMENTBLOCK'); string=@('SCE_P_STRING','SCE_P_CHARACTER','SCE_P_TRIPLE','SCE_P_TRIPLEDOUBLE','SCE_P_FSTRING','SCE_P_FCHARACTER','SCE_P_FTRIPLE','SCE_P_FTRIPLEDOUBLE'); number=@('SCE_P_NUMBER'); keyword=@('SCE_P_WORD'); type=@('SCE_P_CLASSNAME'); function=@('SCE_P_DEFNAME','SCE_P_WORD2'); macro=@('SCE_P_DECORATOR') }
    json = [ordered]@{ comment=@('SCE_JSON_LINECOMMENT','SCE_JSON_BLOCKCOMMENT'); string=@('SCE_JSON_STRING'); escape=@('SCE_JSON_ESCAPESEQUENCE'); variable=@('SCE_JSON_PROPERTYNAME'); number=@('SCE_JSON_NUMBER'); keyword=@('SCE_JSON_KEYWORD') }
    markdown = [ordered]@{ markupHeading=@('SCE_MARKDOWN_HEADER1','SCE_MARKDOWN_HEADER2','SCE_MARKDOWN_HEADER3','SCE_MARKDOWN_HEADER4','SCE_MARKDOWN_HEADER5','SCE_MARKDOWN_HEADER6'); markupBold=@('SCE_MARKDOWN_STRONG1','SCE_MARKDOWN_STRONG2'); markupItalic=@('SCE_MARKDOWN_EM1','SCE_MARKDOWN_EM2'); markupList=@('SCE_MARKDOWN_ULIST_ITEM','SCE_MARKDOWN_OLIST_ITEM'); markupQuote=@('SCE_MARKDOWN_BLOCKQUOTE'); type=@('SCE_MARKDOWN_LINK'); string=@('SCE_MARKDOWN_CODE','SCE_MARKDOWN_CODE2','SCE_MARKDOWN_CODEBK') }
    latex = [ordered]@{ comment=@('SCE_L_COMMENT','SCE_L_COMMENT2'); function=@('SCE_L_COMMAND','SCE_L_SHORTCMD'); type=@('SCE_L_TAG','SCE_L_TAG2'); number=@('SCE_L_MATH','SCE_L_MATH2'); string=@('SCE_L_VERBATIM'); parameter=@('SCE_L_CMDOPT') }
    properties = [ordered]@{ comment=@('SCE_PROPS_COMMENT'); type=@('SCE_PROPS_SECTION'); variable=@('SCE_PROPS_KEY'); foreground=@('SCE_PROPS_ASSIGNMENT'); string=@('SCE_PROPS_DEFVAL') }
    html = [ordered]@{ comment=@('SCE_H_COMMENT'); string=@('SCE_H_DOUBLESTRING','SCE_H_SINGLESTRING'); declaration=@('SCE_H_TAG'); type=@('SCE_H_ATTRIBUTE'); number=@('SCE_H_ENTITY') }
    css = [ordered]@{ comment=@('SCE_CSS_COMMENT'); string=@('SCE_CSS_DOUBLESTRING','SCE_CSS_SINGLESTRING','SCE_CSS_VALUE'); declaration=@('SCE_CSS_TAG'); type=@('SCE_CSS_CLASS'); variable=@('SCE_CSS_IDENTIFIER','SCE_CSS_VARIABLE') }
    yaml = [ordered]@{ comment=@('SCE_YAML_COMMENT'); variable=@('SCE_YAML_IDENTIFIER'); number=@('SCE_YAML_NUMBER'); keyword=@('SCE_YAML_KEYWORD') }
    powershell = [ordered]@{ comment=@('SCE_POWERSHELL_COMMENT','SCE_POWERSHELL_COMMENTSTREAM'); string=@('SCE_POWERSHELL_STRING','SCE_POWERSHELL_CHARACTER','SCE_POWERSHELL_HERE_STRING','SCE_POWERSHELL_HERE_CHARACTER'); keyword=@('SCE_POWERSHELL_KEYWORD'); variable=@('SCE_POWERSHELL_VARIABLE'); function=@('SCE_POWERSHELL_CMDLET') }
    rust = [ordered]@{ comment=@('SCE_RUST_COMMENTBLOCK','SCE_RUST_COMMENTLINE','SCE_RUST_COMMENTBLOCKDOC','SCE_RUST_COMMENTLINEDOC'); string=@('SCE_RUST_STRING','SCE_RUST_STRINGR','SCE_RUST_CHARACTER'); keyword=@('SCE_RUST_WORD','SCE_RUST_WORD3'); type=@('SCE_RUST_WORD2'); number=@('SCE_RUST_NUMBER'); macro=@('SCE_RUST_MACRO') }
    shellscript = [ordered]@{ comment=@('SCE_SH_COMMENTLINE'); string=@('SCE_SH_STRING','SCE_SH_CHARACTER','SCE_SH_BACKTICKS'); keyword=@('SCE_SH_WORD'); variable=@('SCE_SH_SCALAR') }
    sql = [ordered]@{ comment=@('SCE_SQL_COMMENT','SCE_SQL_COMMENTLINE','SCE_SQL_COMMENTDOC'); string=@('SCE_SQL_STRING','SCE_SQL_CHARACTER'); keyword=@('SCE_SQL_WORD'); number=@('SCE_SQL_NUMBER') }
    cmake = [ordered]@{ comment=@('SCE_CMAKE_COMMENT'); string=@('SCE_CMAKE_STRINGDQ','SCE_CMAKE_STRINGLQ','SCE_CMAKE_STRINGRQ'); function=@('SCE_CMAKE_COMMANDS'); parameter=@('SCE_CMAKE_PARAMETERS'); variable=@('SCE_CMAKE_VARIABLE'); number=@('SCE_CMAKE_NUMBER') }
    toml = [ordered]@{ comment=@('SCE_TOML_COMMENT'); string=@('SCE_TOML_STRING_SQ','SCE_TOML_STRING_DQ','SCE_TOML_TRIPLE_STRING_SQ','SCE_TOML_TRIPLE_STRING_DQ'); variable=@('SCE_TOML_KEY'); type=@('SCE_TOML_TABLE'); number=@('SCE_TOML_NUMBER') }
    lua = [ordered]@{ comment=@('SCE_LUA_COMMENT','SCE_LUA_COMMENTLINE','SCE_LUA_COMMENTDOC'); string=@('SCE_LUA_STRING','SCE_LUA_CHARACTER','SCE_LUA_LITERALSTRING'); number=@('SCE_LUA_NUMBER'); keyword=@('SCE_LUA_WORD'); function=@('SCE_LUA_WORD2','SCE_LUA_WORD3','SCE_LUA_WORD4'); preprocessor=@('SCE_LUA_PREPROCESSOR') }
    ruby = [ordered]@{ comment=@('SCE_RB_COMMENTLINE','SCE_RB_POD'); string=@('SCE_RB_STRING','SCE_RB_CHARACTER','SCE_RB_HERE_Q','SCE_RB_HERE_QQ','SCE_RB_STRING_Q','SCE_RB_STRING_QQ'); escape=@('SCE_RB_REGEX','SCE_RB_STRING_QR'); number=@('SCE_RB_NUMBER'); keyword=@('SCE_RB_WORD','SCE_RB_WORD_DEMOTED'); type=@('SCE_RB_CLASSNAME','SCE_RB_MODULE_NAME'); function=@('SCE_RB_DEFNAME'); variable=@('SCE_RB_GLOBAL','SCE_RB_INSTANCE_VAR','SCE_RB_CLASS_VAR') }
    perl = [ordered]@{ comment=@('SCE_PL_COMMENTLINE','SCE_PL_POD'); string=@('SCE_PL_STRING','SCE_PL_CHARACTER','SCE_PL_LONGQUOTE','SCE_PL_HERE_Q','SCE_PL_HERE_QQ','SCE_PL_STRING_Q','SCE_PL_STRING_QQ'); escape=@('SCE_PL_REGEX','SCE_PL_REGSUBST','SCE_PL_STRING_QR'); number=@('SCE_PL_NUMBER'); keyword=@('SCE_PL_WORD'); variable=@('SCE_PL_SCALAR','SCE_PL_ARRAY','SCE_PL_HASH') }
    r = [ordered]@{ comment=@('SCE_R_COMMENT'); string=@('SCE_R_STRING','SCE_R_STRING2','SCE_R_RAWSTRING','SCE_R_RAWSTRING2'); escape=@('SCE_R_ESCAPESEQUENCE'); number=@('SCE_R_NUMBER'); keyword=@('SCE_R_KWORD','SCE_R_BASEKWORD','SCE_R_OTHERKWORD'); variable=@('SCE_R_IDENTIFIER') }
    dart = [ordered]@{ comment=@('SCE_DART_COMMENTLINE','SCE_DART_COMMENTLINEDOC','SCE_DART_COMMENTBLOCK','SCE_DART_COMMENTBLOCKDOC'); string=@('SCE_DART_STRING_SQ','SCE_DART_STRING_DQ','SCE_DART_TRIPLE_STRING_SQ','SCE_DART_TRIPLE_STRING_DQ','SCE_DART_RAWSTRING_SQ','SCE_DART_RAWSTRING_DQ'); escape=@('SCE_DART_ESCAPECHAR'); number=@('SCE_DART_NUMBER'); keyword=@('SCE_DART_KW_PRIMARY','SCE_DART_KW_SECONDARY','SCE_DART_KW_TERTIARY'); type=@('SCE_DART_KW_TYPE'); macro=@('SCE_DART_METADATA') }
    fsharp = [ordered]@{ comment=@('SCE_FSHARP_COMMENT','SCE_FSHARP_COMMENTLINE'); string=@('SCE_FSHARP_STRING','SCE_FSHARP_CHARACTER','SCE_FSHARP_VERBATIM','SCE_FSHARP_QUOTATION'); number=@('SCE_FSHARP_NUMBER'); keyword=@('SCE_FSHARP_KEYWORD','SCE_FSHARP_KEYWORD2','SCE_FSHARP_KEYWORD3','SCE_FSHARP_KEYWORD4','SCE_FSHARP_KEYWORD5'); preprocessor=@('SCE_FSHARP_PREPROCESSOR'); macro=@('SCE_FSHARP_ATTRIBUTE') }
    coffeescript = [ordered]@{ comment=@('SCE_COFFEESCRIPT_COMMENT','SCE_COFFEESCRIPT_COMMENTLINE','SCE_COFFEESCRIPT_COMMENTDOC','SCE_COFFEESCRIPT_COMMENTBLOCK'); string=@('SCE_COFFEESCRIPT_STRING','SCE_COFFEESCRIPT_CHARACTER','SCE_COFFEESCRIPT_VERBATIM','SCE_COFFEESCRIPT_STRINGRAW'); escape=@('SCE_COFFEESCRIPT_REGEX','SCE_COFFEESCRIPT_VERBOSE_REGEX'); number=@('SCE_COFFEESCRIPT_NUMBER'); keyword=@('SCE_COFFEESCRIPT_WORD','SCE_COFFEESCRIPT_WORD2'); type=@('SCE_COFFEESCRIPT_GLOBALCLASS'); variable=@('SCE_COFFEESCRIPT_INSTANCEPROPERTY') }
    makefile = [ordered]@{ comment=@('SCE_MAKE_COMMENT'); preprocessor=@('SCE_MAKE_PREPROCESSOR'); variable=@('SCE_MAKE_IDENTIFIER'); function=@('SCE_MAKE_TARGET') }
    diff = [ordered]@{ comment=@('SCE_DIFF_COMMENT'); preprocessor=@('SCE_DIFF_COMMAND','SCE_DIFF_HEADER'); type=@('SCE_DIFF_POSITION'); string=@('SCE_DIFF_DELETED','SCE_DIFF_PATCH_DELETE','SCE_DIFF_REMOVED_PATCH_DELETE'); number=@('SCE_DIFF_ADDED','SCE_DIFF_PATCH_ADD','SCE_DIFF_REMOVED_PATCH_ADD'); keyword=@('SCE_DIFF_CHANGED') }
    asm = [ordered]@{ comment=@('SCE_ASM_COMMENT','SCE_ASM_COMMENTBLOCK'); string=@('SCE_ASM_STRING','SCE_ASM_CHARACTER','SCE_ASM_STRINGBACKQUOTE'); number=@('SCE_ASM_NUMBER'); keyword=@('SCE_ASM_CPUINSTRUCTION','SCE_ASM_MATHINSTRUCTION','SCE_ASM_EXTINSTRUCTION'); variable=@('SCE_ASM_REGISTER'); preprocessor=@('SCE_ASM_DIRECTIVE') }
    julia = [ordered]@{ comment=@('SCE_JULIA_COMMENT','SCE_JULIA_DOCSTRING'); string=@('SCE_JULIA_STRING','SCE_JULIA_CHAR','SCE_JULIA_STRINGLITERAL'); number=@('SCE_JULIA_NUMBER'); keyword=@('SCE_JULIA_KEYWORD1','SCE_JULIA_KEYWORD2','SCE_JULIA_KEYWORD3','SCE_JULIA_KEYWORD4'); type=@('SCE_JULIA_TYPEANNOT'); macro=@('SCE_JULIA_MACRO') }
}

$aliases = [ordered]@{
    c='cpp'; 'objective-c'='cpp'; 'objective-cpp'='cpp'; 'cuda-cpp'='cpp'; glsl='cpp'; hlsl='cpp'; java='cpp'; csharp='cpp'; go='cpp'; kotlin='cpp'; scala='cpp'; swift='cpp'; groovy='cpp'; javascript='cpp'; javascriptreact='cpp'; typescript='cpp'; typescriptreact='cpp'
    jsonc='json'; jsonl='json'; snippets='json'; mermaid='markdown'; prompt='markdown'; instructions='markdown'; skill='markdown'; tex='latex'; bibtex='latex'
    'git-config'='properties'; ini='properties'; dotenv='properties'; ignore='properties'; 'git-commit'='properties'; 'git-rebase'='properties'
    xml='html'; php='html'; razor='html'; vue='html'; svelte='html'; astro='html'; scss='css'; less='css'; dockercompose='yaml'; dockerfile='shellscript'; raku='perl'
    asciidoc='markdown'; juliamarkdown='markdown'; 'markdown-math'='markdown'; 'jsx-tags'='cpp'; shaderlab='cpp'; handlebars='html'; jade='html'
}

$metadataStyled = @('bat', 'clojure', 'd', 'fortran', 'haskell', 'lisp', 'nim',
    'pascal', 'tcl', 'vb', 'verilog', 'vhdl', 'xsl', 'zig')

$styleAttributes = @{
    markdown = @{
        markupHeading = @{ bold=$true }
        markupBold = @{ bold=$true }
        markupItalic = @{ italic=$true }
        type = @{ underline=$true }
    }
}

$outputRoot = Join-Path $SourceRoot "src\editor\resources\themes\languages"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

function Resolve-StyleIds([object[]]$names) {
    @($names | ForEach-Object {
        if (-not $definitions.ContainsKey($_)) { throw "Unknown Lexilla style constant: $_" }
        $definitions[$_]
    } | Sort-Object -Unique)
}

foreach ($entry in $profiles.GetEnumerator()) {
    $styles = [ordered]@{}
    foreach ($role in $entry.Value.GetEnumerator()) {
        $ids = Resolve-StyleIds $role.Value
        $languageAttributes = $styleAttributes[$entry.Key]
        $attributes = if ($languageAttributes) { $languageAttributes[$role.Key] } else { $null }
        if ($attributes) {
            $style = [ordered]@{ ids=$ids }
            foreach ($attribute in $attributes.GetEnumerator()) {
                $style[$attribute.Key] = $attribute.Value
            }
            $styles[$role.Key] = $style
        } else {
            $styles[$role.Key] = $ids
        }
    }
    $document = [ordered]@{ id=$entry.Key; lexer=$entry.Key; styles=$styles }
    $document | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot "$($entry.Key).json") -Encoding utf8
}

foreach ($entry in $aliases.GetEnumerator()) {
    $document = [ordered]@{ id=$entry.Key; inherits=$entry.Value }
    $document | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot "$($entry.Key).json") -Encoding utf8
}

foreach ($id in $metadataStyled) {
    $document = [ordered]@{ id=$id; lexer=$id; styles=[ordered]@{} }
    $document | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot "$id.json") -Encoding utf8
}

$resourceRoot = Join-Path $SourceRoot "src\editor\resources\themes"
$resourceFiles = Get-ChildItem -Path $resourceRoot -Filter *.json -Recurse |
    ForEach-Object { $_.FullName.Substring($resourceRoot.Length + 1).Replace('\', '/') } |
    Sort-Object
$qrc = @('<RCC>', '  <qresource prefix="/litecode/themes">')
foreach ($resource in $resourceFiles) {
    $qrc += "    <file>$resource</file>"
}
$qrc += @('  </qresource>', '</RCC>')
$qrc | Set-Content -LiteralPath (Join-Path $resourceRoot "LiteCodeThemes.qrc") -Encoding utf8

Write-Output "Generated $($profiles.Count + $aliases.Count + $metadataStyled.Count) LiteCode Lexilla theme files in $outputRoot"
