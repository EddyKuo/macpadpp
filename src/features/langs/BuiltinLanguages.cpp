#include "features/langs/BuiltinLanguages.h"

#include <iterator>

#include <QHash>
#include <QSet>

namespace macpad::features {

namespace {

// C 系語言共用運算子集合（UdlLexer 會依長度遞減比對，最長匹配優先）
constexpr const char *kCOperators =
    "+ - * / % = == != < > <= >= && || ! & | ^ ~ << >> ++ -- += -= *= /= %= -> => :: ? : ; , . ( ) [ ] { }";
constexpr const char *kBasicOperators =
    "+ - * / \\ ^ = <> < > <= >= ( ) , . & :";
constexpr const char *kLispOperators = "( ) ' ` , @";

// 單一內建語言的靜態描述。keywords 以空白分隔；空字串代表該欄不適用。
struct Spec {
    const char *key;
    const char *display;
    const char *exts;          // 空白分隔，不含點
    const char *kw1;           // 關鍵字群組 0（語言關鍵字）
    const char *kw2;           // 關鍵字群組 1（型別/內建函式/常數），可為空
    const char *lineComment;
    const char *blockStart;
    const char *blockEnd;
    bool caseSensitive;
    const char *foldOpen;      // 空字串代表不做符號摺疊
    const char *foldClose;
    const char *operators;
};

// 依 display 名稱排序，讓 Language 選單維持字母序。
const Spec kSpecs[] = {

// ── A ────────────────────────────────────────────────────────────────────────
{"actionscript", "ActionScript", "as mx",
 "break case catch class const continue default delete do dynamic each else extends false final finally for function get if implements import in include instanceof interface internal is namespace native new null override package private protected public return set static super switch this throw true try typeof use var void while with",
 "Array Boolean Class Date Error Function int Number Object RegExp String uint Vector XML",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"ada", "Ada", "ada ads adb",
 "abort abs abstract accept access aliased all and array at begin body case constant declare delay delta digits do else elsif end entry exception exit for function generic goto if in interface is limited loop mod new not null of or others out overriding package pragma private procedure protected raise range record rem renames requeue return reverse select separate some subtype synchronized tagged task terminate then type until use when while with xor",
 "Boolean Character Duration Float Integer Long_Float Long_Integer Natural Positive String Wide_Character Wide_String",
 "--", "", "", false, "", "", kBasicOperators},

{"asn1", "ASN.1", "asn1 mib",
 "ABSENT ALL ANY APPLICATION AUTOMATIC BEGIN BY CHOICE CLASS COMPONENT COMPONENTS CONSTRAINED CONTAINING DEFAULT DEFINITIONS EMBEDDED ENCODED END ENUMERATED EXCEPT EXPLICIT EXPORTS EXTENSIBILITY FROM IDENTIFIER IMPLICIT IMPORTS INCLUDES INSTANCE INTERSECTION MAX MIN MODULE OF OPTIONAL PATTERN PDV PRESENT PRIVATE SEQUENCE SET SIZE STRING SYNTAX TAGS UNION UNIQUE UNIVERSAL WITH",
 "BIT BOOLEAN CHARACTER DATE INTEGER NULL OBJECT OCTET REAL RELATIVE-OID TIME UTCTime",
 "--", "", "", true, "{", "}", kBasicOperators},

{"asm", "Assembly", "asm s inc nasm mips",
 "aaa aad aam aas adc add and call cbw clc cld cli cmc cmp cmps cwd daa das dec div enter esc hlt idiv imul in inc int into iret ja jae jb jbe jc jcxz je jg jge jl jle jmp jna jnae jnb jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo js jz lahf lds lea les lock lods loop loope loopne loopnz loopz mov movs movsx movzx mul neg nop not or out pop popa popf push pusha pushf rcl rcr rep repe repne repnz repz ret rol ror sahf sal sar sbb scas shl shr stc std sti stos sub test wait xchg xlat xor",
 "ah al ax bh bl bp bx ch cl cs cx dh di dl ds dx eax ebp ebx ecx edi edx es esi esp fs gs rax rbp rbx rcx rdi rdx rsi rsp si sp ss byte word dword qword ptr offset section global extern db dw dd dq equ times org",
 ";", "", "", false, "", "", "+ - * / , [ ] : ( )"},

{"asp", "ASP", "asp aspx asa",
 "and byref byval call case class const dim do each else elseif empty end eqv erase error exit false for function get if imp in is let loop mod new next not nothing null on option or preserve private property public randomize redim rem resume select set sub then to true until wend while with xor",
 "Application Request Response Server Session ObjectContext Err",
 "'", "", "", false, "", "", kBasicOperators},

{"autoit", "AutoIt", "au3",
 "and byref case const continuecase continueloop default dim do else elseif endfunc endif endselect endswitch endwith enum exit exitloop false for func global if in local next not null or redim return select static step switch then to true until volatile wend while with",
 "@AppDataDir @ComputerName @CRLF @DesktopDir @error @extended @HomeDrive @HotKeyPressed @LF @OSVersion @ScriptDir @ScriptName @SW_HIDE @SW_SHOW @TAB @TempDir @UserName @WindowsDir",
 ";", "#cs", "#ce", false, "", "", kBasicOperators},

{"avs", "AviSynth", "avs avsi",
 "catch else false for function global if load_plugin return true try while",
 "AudioDub Amplify AviSource BlankClip Crop DirectShowSource FadeIn FadeOut ImageSource Import Interleave Levels Overlay Resize Reverse SelectEven SelectOdd Subtitle Trim WavSource",
 "#", "/*", "*/", false, "", "", kCOperators},

// ── B ────────────────────────────────────────────────────────────────────────
{"baanc", "BaanC", "bc cln",
 "before after break case continue declaration default do domain else endcase endfor endif endwhile extern for function if long return select selectdo selectempty selecteos selectempty static string table then while",
 "boolean double float integer long string",
 "|", "", "", false, "", "", kCOperators},

{"blitzbasic", "BlitzBasic", "bb",
 "and case const data default delete dim each else elseif end endif exit false field first for forever function global gosub goto if include insert last local new next not null or read repeat restore return select step str then to true type until wend while xor",
 "Byte Float Int Short String Abs Cos Rand Sin Sqr Tan",
 ";", "", "", false, "", "", kBasicOperators},

// ── C ────────────────────────────────────────────────────────────────────────
{"cobol", "COBOL", "cbl cob cpy cbd cdb cdc lst",
 "accept add alter call cancel close compute continue delete display divide else end evaluate exit go goback if initialize inspect merge move multiply open perform read release return rewrite search set sort start stop string subtract unstring write",
 "author configuration data division environment file-control identification input-output linkage local-storage procedure program-id section select storage using working-storage pic picture value comp comp-3 occurs redefines",
 "*>", "", "", false, "", "", "+ - * / = < > ( ) ."},

{"csound", "Csound", "orc sco csd",
 "endin endop instr opcode if igoto goto kgoto then elseif else endif until do od",
 "a4 ftlen ftsr kr ksmps nchnls sr 0dbfs oscil oscili linen line expon madsr out outs poscil vco2",
 ";", "/*", "*/", true, "", "", kCOperators},

// ── D ────────────────────────────────────────────────────────────────────────
{"dart", "Dart", "dart",
 "abstract as assert async await base break case catch class const continue covariant default deferred do dynamic else enum export extends extension external factory false final finally for get hide if implements import in interface is late library mixin new null on operator part required rethrow return sealed set show static super switch sync this throw true try typedef var void when while with yield",
 "bool double int num String List Map Set Object Future Stream Iterable Symbol Type Function Never Record",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── E ────────────────────────────────────────────────────────────────────────
{"erlang", "Erlang", "erl hrl escript",
 "after and andalso band begin bnot bor bsl bsr bxor case catch cond div end fun if let not of or orelse receive rem try when xor",
 "atom binary bitstring boolean float integer list map pid port reference tuple erlang lists io gen_server spawn self node",
 "%", "", "", true, "", "", "+ - * / = == /= =< >= < > ++ -- ! -> <- : ; , . ( ) [ ] { } |"},

{"errorlist", "Error List", "err log",
 "error warning note fatal",
 "",
 "", "", "", false, "", "", ""},

{"escseq", "ANSI Escape Sequence", "ans",
 "",
 "",
 "", "", "", true, "", "", ""},

{"escript", "ESCRIPT", "em src",
 "and array break case const continue default dictionary do downto else elseif end endcase endenum endfor endfunction endif endprogram endrepeat endswitch endwhile enum exit for foreach function global if in include local next not or program repeat return step switch to until use var while",
 "error print set_critical sleep start_script wait_for_event",
 "//", "/*", "*/", false, "", "", kCOperators},

// ── F ────────────────────────────────────────────────────────────────────────
{"forth", "Forth", "forth 4th fth",
 ": ; if else then begin until while repeat do loop +loop leave exit variable constant create does> allot cells immediate recurse case of endof endcase",
 "dup drop swap over rot -rot nip tuck depth emit key cr space spaces type words see",
 "\\", "(", ")", false, "", "", "+ - * / = < > @ !"},

{"freebasic", "FreeBasic", "bi bas",
 "and as byref byval call case class const continue declare dim do else elseif end enum exit extern for function goto if implements import namespace next not or private property protected public return scope select shared static step sub then to type union until using var wend while with xor",
 "Boolean Byte Double Integer Long LongInt Short Single String UByte UInteger ULong UShort ZString WString",
 "'", "/'", "'/", false, "", "", kBasicOperators},

// ── G ────────────────────────────────────────────────────────────────────────
{"gap", "GAP", "g gi gd",
 "and do elif else end fail false fi for function if in local mod not od or rec repeat return then true until while",
 "Add Append Display Error Filtered ForAll ForAny Length List Print Set Size Sort Sum",
 "#", "", "", true, "", "", kBasicOperators},

{"gdscript", "GDScript", "gd",
 "and as assert await break breakpoint class class_name const continue elif else enum extends export func if in is match not or pass preload return self signal static super tool var while yield true false null",
 "Array bool Callable Color Dictionary float int Node Node2D Node3D Object PackedScene Resource RID Signal String StringName Transform2D Transform3D Vector2 Vector3 Vector4",
 "#", "", "", true, "", "", kCOperators},

{"glsl", "GLSL", "glsl vert frag geom tesc tese comp",
 "attribute break case centroid const continue default discard do else false flat for highp if in inout invariant layout lowp mediump noperspective out patch precision return sample sampler smooth struct subroutine switch true uniform varying while",
 "bool bvec2 bvec3 bvec4 dmat2 dmat3 dmat4 double dvec2 dvec3 dvec4 float int isampler2D ivec2 ivec3 ivec4 mat2 mat3 mat4 sampler1D sampler2D sampler3D samplerCube uint usampler2D uvec2 uvec3 uvec4 vec2 vec3 vec4 void gl_FragColor gl_Position gl_VertexID",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"gnuplot", "Gnuplot", "plt gp gnuplot",
 "bind call cd clear do else exit fit for help history if load lower pause plot print pwd quit raise refresh replot reread reset save set shell show splot stats system test undefine unset update while",
 "autoscale border grid key label logscale output pointsize samples style terminal title xlabel xrange ylabel yrange zlabel",
 "#", "", "", true, "", "", kBasicOperators},

{"go", "Go", "go",
 "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var",
 "any append bool byte cap clear close comparable complex complex64 complex128 copy delete error false float32 float64 imag int int8 int16 int32 int64 iota len make max min new nil panic print println real recover rune string true uint uint8 uint16 uint32 uint64 uintptr",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"groovy", "Groovy", "groovy gvy gy gradle",
 "abstract as assert boolean break byte case catch char class const continue def default do double else enum extends false final finally float for goto if implements import in instanceof int interface long native new null package private protected public return short static strictfp super switch synchronized this threadsafe throw throws trait transient true try void volatile while",
 "BigDecimal BigInteger Boolean Byte Character Closure Double File Float Integer List Long Map Number Object Range String StringBuffer",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"gui4cli", "Gui4Cli", "gc",
 "if else endif goto gosub return end use load close set get quit",
 "window text button box line image",
 ";", "", "", false, "", "", kBasicOperators},

// ── H ────────────────────────────────────────────────────────────────────────
{"haskell", "Haskell", "hs lhs",
 "case class data default deriving do else foreign if import in infix infixl infixr instance let mdo module newtype of proc rec then type where",
 "Bool Char Double Either Float Int Integer IO Maybe Ordering Rational String Word concat filter foldl foldr head length map mapM print putStrLn return show tail",
 "--", "{-", "-}", true, "", "", "+ - * / = == /= < > <= >= && || ++ . $ :: -> <- => | @ ~ \\ ( ) [ ] , ;"},

{"haxe", "Haxe", "hx hxml",
 "abstract break case cast catch class continue default do dynamic else enum extends extern false final for function if implements import in inline interface macro new null operator overload override package private public return static super switch this throw true try typedef untyped using var while",
 "Any Array Bool Class Date Dynamic EReg Float Int Iterator Map Null String StringBuf Void",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── I ────────────────────────────────────────────────────────────────────────
{"inno", "Inno Setup", "iss",
 "begin break case const continue do downto else end except finally for function if of procedure repeat then to try type until uses var while with",
 "AppName AppVersion DefaultDirName DefaultGroupName Files Icons Languages Run Setup Tasks UninstallDelete Components Dirs Messages Registry",
 ";", "{", "}", false, "", "", kBasicOperators},

{"intercal", "INTERCAL", "i",
 "DO PLEASE NOT NEXT RESUME FORGET STASH RETRIEVE IGNORE REMEMBER ABSTAIN REINSTATE GIVE UP WRITE IN READ OUT COME FROM",
 "",
 "", "", "", false, "", "", ""},

// ── J ────────────────────────────────────────────────────────────────────────
{"jsp", "JSP", "jsp jspf",
 "abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while",
 "page include taglib useBean setProperty getProperty forward plugin request response session out application config pageContext",
 "//", "<%--", "--%>", true, "{", "}", kCOperators},

// ── K ────────────────────────────────────────────────────────────────────────
{"kixtart", "KiXtart", "kix",
 "break case do each else endif endselect exit for function endfunction get gets global go gosub goto if loop next return run select set setl setm shell sleep until use while",
 "@date @time @userid @wksta @homedir @domain @fullname @ipaddress @scriptdir",
 ";", "/*", "*/", false, "", "", kBasicOperators},

{"kotlin", "Kotlin", "kt kts",
 "abstract actual annotation as break by catch class companion const constructor continue crossinline data delegate do dynamic else enum expect external false field file final finally for fun get if import in infix init inline inner interface internal is it lateinit noinline null object open operator out override package param private property protected public receiver reified return sealed set setparam super suspend tailrec this throw true try typealias typeof val value var vararg when where while",
 "Any Array Boolean Byte Char CharSequence Comparable Double Float Int Iterable List Long Map MutableList MutableMap MutableSet Nothing Number Pair Sequence Set Short String Triple Unit",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── L ────────────────────────────────────────────────────────────────────────
{"lisp", "Lisp", "lsp lisp el cl",
 "and cond defmacro defparameter defun defvar do dolist dotimes eval if lambda let let* loop not or progn quote return setf setq unless when",
 "apply append atom car cdr cons eq equal format funcall length list listp mapcar member nil nth null princ print reverse t",
 ";", "#|", "|#", false, "(", ")", kLispOperators},

{"locoscript", "LocoScript", "loco",
 "if else endif while wend do loop return",
 "",
 "'", "", "", false, "", "", kBasicOperators},

{"luau", "Luau", "luau",
 "and break continue do else elseif end export false for function if in local nil not or repeat return then true type until while",
 "assert error ipairs next pairs pcall print require select setmetatable tonumber tostring type unpack buffer coroutine math os string table task",
 "--", "--[[", "]]", true, "", "", kCOperators},

// ── M ────────────────────────────────────────────────────────────────────────
{"maxscript", "MAXScript", "ms mcr mzp",
 "about and animate as at by case catch collect continue coordsys do else exit fn for function global if in local macroscript mapped max not of on or parameters persistent plugin rcmenu return rollout set struct then throw to tool try undo utility when where while with",
 "box cylinder sphere teapot point3 color matrix3 quat time selection objects rootnode meshop polyop",
 "--", "/*", "*/", false, "", "", kCOperators},

{"mel", "MEL", "mel",
 "alias break case continue default do else float global if in int matrix proc return string switch vector while",
 "getAttr setAttr select ls polyCube polySphere xform connectAttr createNode delete file listRelatives",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"mmixal", "MMIXAL", "mms",
 "ADD AND BNZ BZ CMP DIV GET GETA GO INCH INCL INCML INCMH IS JMP LDA LDB LDO LDT LDW LOC MUL NEG OCTA ORH PBNZ POP PREFIX PUSHJ PUT SET STB STO STT STW SUB SWYM TETRA TRAP WYDE",
 "rA rB rD rE rH rJ rM rR Fputs Halt StdOut",
 "%", "", "", false, "", "", "+ - * / , : @ $"},

{"modula3", "Modula-3", "m3 i3 ig mg",
 "AND ANY ARRAY AS BEGIN BITS BRANDED BY CASE CONST DIV DO ELSE ELSIF END EVAL EXCEPT EXCEPTION EXIT EXPORTS FINALLY FOR FROM GENERIC IF IMPORT IN INTERFACE LOCK LOOP METHODS MOD MODULE NOT OBJECT OF OR OVERRIDES PROCEDURE RAISE RAISES READONLY RECORD REF REPEAT RETURN REVEAL ROOT SET THEN TO TRY TYPE TYPECASE UNSAFE UNTIL UNTRACED VALUE VAR WHILE WITH",
 "ADDRESS BOOLEAN CARDINAL CHAR EXTENDED FALSE INTEGER LONGINT LONGREAL MUTEX NIL NULL REAL REFANY TEXT TRUE",
 "", "(*", "*)", true, "", "", kBasicOperators},

{"mupad", "MuPAD", "mu",
 "and assuming break case do downto elif else end end_case end_for end_if end_proc end_repeat end_while for from if in local mod next not of or otherwise proc quit repeat return step then until while",
 "DIGITS FALSE PI TRUE UNKNOWN diff expand float int limit matrix plot simplify solve subs sum",
 "//", "/*", "*/", true, "", "", kBasicOperators},

// ── N ────────────────────────────────────────────────────────────────────────
{"netrexx", "NetRexx", "nrx",
 "catch class do else end exit finally if import loop method nop numeric options otherwise package parse property return say select signal then trace when",
 "Rexx String boolean byte char double float int long short void",
 "--", "/*", "*/", false, "", "", kBasicOperators},

{"nginx", "nginx", "nginx conf",
 "http server location upstream events mail stream if include return rewrite set map geo limit_except types",
 "access_log add_header alias auth_basic client_max_body_size default_type error_log error_page expires fastcgi_pass gzip index keepalive_timeout listen proxy_pass proxy_set_header root sendfile server_name ssl_certificate ssl_certificate_key try_files worker_connections worker_processes",
 "#", "", "", true, "{", "}", "; { }"},

{"nim", "Nim", "nim nims nimble",
 "addr and as asm bind block break case cast concept const continue converter defer discard distinct div do elif else end enum except export finally for from func if import in include interface is isnot iterator let macro method mixin mod nil not notin object of or out proc ptr raise ref return shl shr static template try tuple type using var when while xor yield",
 "array bool byte char cstring float float32 float64 int int8 int16 int32 int64 openarray Ordinal pointer range seq set string uint uint8 uint16 uint32 uint64 varargs echo len new result",
 "#", "#[", "]#", true, "", "", kCOperators},

{"nncrontab", "Nncrontab", "tab",
 "MAILTO PATH SHELL reboot yearly annually monthly weekly daily midnight hourly",
 "",
 "#", "", "", true, "", "", "* / , -"},

{"nsis", "NSIS", "nsi nsh",
 "Function FunctionEnd Section SectionEnd SectionGroup SectionGroupEnd SubSection SubSectionEnd Goto IfErrors IfFileExists IfSilent IntCmp StrCmp Return Abort Call ClearErrors Delete DetailPrint Exec ExecWait File MessageBox Pop Push Quit ReadRegStr RMDir SetOutPath Sleep StrCpy WriteRegStr WriteUninstaller",
 "Name OutFile InstallDir InstallDirRegKey RequestExecutionLevel ShowInstDetails Var LangString Icon Caption BrandingText AllowRootDirInstall AutoCloseWindow",
 ";", "/*", "*/", false, "", "", kBasicOperators},

{"nxc", "NXC", "nxc",
 "asm break case const continue default do else false for goto if inline repeat return safecall struct sub switch task true typedef until void while",
 "bool byte char int long mutex string unsigned Wait OnFwd OnRev Off Float PlaySound TextOut NumOut ClearScreen",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── O ────────────────────────────────────────────────────────────────────────
{"oscript", "OScript", "osx os",
 "break by continue default downto else elseif end for function if in repeat return script switch to until while",
 "Assoc Boolean Bytes Date Dynamic Error File Integer List Object Real RecArray Record String Void",
 "//", "/*", "*/", false, "", "", kCOperators},

// ── P ────────────────────────────────────────────────────────────────────────
{"pawn", "PAWN", "pwn p inc",
 "assert break case const continue default defined do else exit for forward goto if native new operator public return sizeof sleep state static stock switch tagof while",
 "bool Float any char printf print strlen strcat strcmp funcidx",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"pike", "Pike", "pike pmod",
 "array break case catch class constant continue default do else enum extern final for foreach gauge global if import inherit inline int lambda local mapping mixed multiset object optional private program protected public return static string switch typedef typeof variant void while",
 "float function int object program string werror write sprintf sscanf sizeof indices values",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"plm", "PL/M", "plm",
 "ADDRESS AND BASED BY BYTE CALL CASE DATA DECLARE DISABLE DO ELSE ENABLE END EOF EXTERNAL GO GOTO HALT IF INITIAL INTERRUPT LABEL LITERALLY MINUS MOD NOT OR PLUS PROCEDURE PUBLIC REENTRANT RETURN STRUCTURE THEN TO WHILE WORD XOR",
 "",
 "", "/*", "*/", false, "", "", kBasicOperators},

{"pl-sql", "PL/SQL", "pls plsql",
 "begin body case close cursor declare else elsif end exception execute exit fetch for forall function goto if in into is loop open or out package pragma procedure raise return rollback select then trigger type update using when while",
 "boolean char clob date number nvarchar2 pls_integer raw rowid varchar2 sysdate dbms_output raise_application_error commit",
 "--", "/*", "*/", false, "", "", kBasicOperators},

{"powershell", "PowerShell", "ps1 psm1 psd1",
 "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for foreach from function hidden if in inlinescript parallel param process return static switch throw trap try until using var while workflow",
 "Add-Content Add-Member Clear-Host Compare-Object ConvertTo-Json Copy-Item Export-Csv ForEach-Object Get-ChildItem Get-Command Get-Content Get-Date Get-Help Get-Item Get-Location Get-Member Get-Process Get-Service Import-Csv Invoke-Expression Invoke-RestMethod Invoke-WebRequest Join-Path Measure-Object New-Item New-Object Out-File Out-Null Read-Host Remove-Item Rename-Item Select-Object Select-String Set-Content Set-Location Sort-Object Start-Process Stop-Process Test-Path Where-Object Write-Error Write-Host Write-Output Write-Verbose Write-Warning",
 "#", "<#", "#>", false, "{", "}", kCOperators},

{"progress", "Progress", "abl",
 "and assign available avg buffer by call case count create def define delete disp display do down each else end error every find first for form function if in input insert leave message next no-lock no-undo of or output procedure put query recid repeat return run set share-lock skip stream table then this-procedure to trigger undo up update use-index using value var when where while with",
 "character date datetime decimal handle int64 integer logical longchar memptr raw rowid",
 "//", "/*", "*/", false, "", "", kBasicOperators},

{"prolog", "Prolog", "pl pro prolog",
 "is mod rem div abs sign min max not fail true false dynamic discontiguous module use_module initialization",
 "append assert asserta assertz atom atom_codes between findall functor length member nb_getval number retract setof sort var write writeln nl",
 "%", "/*", "*/", true, "", "", ":- ?- , . ; -> = \\= == \\== < > =< >= + - * / ( ) [ ] |"},

{"protobuf", "Protocol Buffers", "proto",
 "enum extend extensions import map message oneof option package public repeated required optional reserved returns rpc service stream syntax to weak",
 "bool bytes double fixed32 fixed64 float int32 int64 sfixed32 sfixed64 sint32 sint64 string uint32 uint64",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"psl", "PSL", "psl",
 "assert assume cover default else endpoint fairness for forall if in inherit is never property restrict sequence strong vmode vprop vunit",
 "always before eventually next never until within rose fell prev stable",
 "//", "/*", "*/", true, "", "", kCOperators},

{"purebasic", "PureBasic", "pb pbi pbf",
 "And Break CallDebugger Case Continue Data DataSection Default Define Dim Else ElseIf EndDataSection EndEnumeration EndIf EndInterface EndMacro EndProcedure EndSelect EndStructure EndWith Enumeration For ForEach Forever Global Gosub Goto If Interface Macro Next NewList Not Or Procedure ProcedureReturn Protected Read ReDim Repeat Restore Return Select Shared Structure Swap To Until Wend While With XIncludeFile",
 "Byte Character Double Float Integer Long Quad String Unicode Word",
 ";", "", "", false, "", "", kBasicOperators},

// ── R ────────────────────────────────────────────────────────────────────────
{"r", "R", "r rdata rds rda",
 "break else for function if in next repeat return while TRUE FALSE NULL Inf NaN NA NA_integer_ NA_real_ NA_character_",
 "abs apply array as.character as.numeric c cbind class colnames data.frame dim factor head install.packages length library list matrix max mean median min names ncol nrow paste plot print rbind read.csv rep rownames sapply seq setwd sd sum summary table tail vector which",
 "#", "", "", true, "{", "}", kCOperators},

{"rebol", "REBOL", "r3 reb",
 "all any break case do either else exit for foreach forever func function if loop repeat return switch throw try unless until use while",
 "append change clear copy find first insert join last length? make mold next now pick print probe reduce remove select sort to-string trim",
 ";", "comment {", "}", false, "[", "]", kBasicOperators},

{"registry", "Windows Registry", "reg",
 "HKEY_CLASSES_ROOT HKEY_CURRENT_CONFIG HKEY_CURRENT_USER HKEY_LOCAL_MACHINE HKEY_USERS HKCR HKCU HKLM HKU REGEDIT4",
 "dword hex qword",
 ";", "", "", false, "[", "]", "= @ , \\"},

{"resource", "Windows Resource", "rc rc2 dlg",
 "ACCELERATORS BEGIN BITMAP BLOCK BUTTON CAPTION CHECKBOX CLASS COMBOBOX CONTROL CTEXT CURSOR DEFPUSHBUTTON DIALOG DIALOGEX DISCARDABLE EDITTEXT END EXSTYLE FONT GROUPBOX ICON LANGUAGE LISTBOX LTEXT MENU MENUITEM POPUP PUSHBUTTON RADIOBUTTON RTEXT SCROLLBAR SEPARATOR STRINGTABLE STYLE VALUE VERSIONINFO",
 "WS_CHILD WS_VISIBLE WS_BORDER WS_CAPTION WS_POPUP WS_SYSMENU BS_AUTOCHECKBOX ES_AUTOHSCROLL SS_LEFT",
 "//", "/*", "*/", false, "", "", kCOperators},

{"rexx", "Rexx", "rex rexx rx",
 "address arg call do drop else end exit if interpret iterate leave nop numeric otherwise parse procedure pull push queue return say select signal then trace when",
 "abs date left length pos right space strip substr time translate value verify word words x2c c2x",
 "--", "/*", "*/", false, "", "", kBasicOperators},

{"robotframework", "Robot Framework", "robot resource",
 "Settings Variables Keywords Tasks Library Resource Suite Setup Teardown Template Timeout Documentation Tags Arguments Return FOR END IF ELSE WHILE TRY EXCEPT",
 "Log Should Be Equal Should Contain Set Variable Run Keyword Sleep Open Browser Click Element Input Text",
 "#", "", "", false, "", "", ""},

{"ruleslanguage", "Rules Language", "rul",
 "and begin call case do else elseif end for function if in not or procedure return then while",
 "boolean date integer number string",
 "//", "/*", "*/", false, "", "", kCOperators},

{"rust", "Rust", "rs",
 "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type union unsafe use where while",
 "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize Box Option Result String Vec HashMap HashSet Rc Arc RefCell Some None Ok Err println! format! vec! panic! assert! write! derive",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── S ────────────────────────────────────────────────────────────────────────
{"sas", "SAS", "sas",
 "and array by cards data delete do drop else end format if in infile input keep label length libname merge not or otherwise output proc put retain run select set then title var where while",
 "abs ceil floor int intck intnx lag left length lowcase max mean min put round scan substr sum trim upcase",
 "*", "/*", "*/", false, "", "", kBasicOperators},

{"sass", "Sass", "sass scss",
 "@at-root @content @debug @each @else @error @extend @for @function @if @import @include @media @mixin @return @use @warn @while and from in not or through to",
 "adjust-color darken lighten map-get mix nth opacify percentage rgba saturate transparentize type-of unit unquote",
 "//", "/*", "*/", false, "{", "}", kCOperators},

{"scala", "Scala", "scala sc",
 "abstract case catch class def do else extends false final finally for forSome given if implicit import lazy match new null object override package private protected return sealed super this throw trait try true type val var while with yield",
 "Any AnyRef AnyVal Boolean Byte Char Double Either Float Int List Long Map Nothing Option Seq Set Short String Try Unit Vector None Some",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"scheme", "Scheme", "scm ss sch",
 "and begin case cond define define-syntax delay do else if lambda let let* letrec or quasiquote quote set! syntax-rules unless when",
 "append apply assoc boolean? car cdr cons display eq? equal? for-each length list list? map max min newline not null? number? pair? procedure? reverse string? symbol? vector",
 ";", "#|", "|#", false, "(", ")", kLispOperators},

{"scilab", "Scilab", "sci sce",
 "abort break case catch continue do else elseif end endfunction for function if pause return select then try while",
 "cos disp exp length linspace log max mean min ones plot rand sin size sqrt sum zeros",
 "//", "/*", "*/", true, "", "", kBasicOperators},

{"smalltalk", "Smalltalk", "st",
 "self super true false nil thisContext",
 "Array Bag Boolean Character Class Collection Date Dictionary Float Fraction Integer Interval Number Object OrderedCollection Set String Symbol Time Transcript",
 "", "\"", "\"", true, "[", "]", "+ - * / = == ~= < > <= >= := ^ ; . ( ) [ ] { }"},

{"snobol", "SNOBOL", "sno",
 "ANY ARB ARBNO BAL BREAK END FAIL FENCE LEN NOTANY POS REM RPOS RTAB SPAN SUCCEED TAB",
 "ARRAY CONVERT DATE DEFINE DUPL IDENT INPUT OUTPUT REPLACE SIZE TABLE TRIM",
 "*", "", "", false, "", "", "= + - / ? $ . , ( )"},

{"solidity", "Solidity", "sol",
 "abstract anonymous as assembly break calldata catch constant constructor continue contract delete do else emit enum event external fallback for function if immutable import indexed interface internal is library mapping memory modifier new override payable pragma private public pure receive return returns revert storage struct try type unchecked using view virtual while",
 "address bool byte bytes bytes32 int int256 string uint uint8 uint256 msg block tx now this super require assert selfdestruct keccak256 ecrecover",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"sparql", "SPARQL", "rq sparql",
 "ASC ASK BASE BIND BINDINGS BY CONSTRUCT DESC DESCRIBE DISTINCT FILTER FROM GRAPH GROUP HAVING INSERT LIMIT MINUS NAMED OFFSET OPTIONAL ORDER PREFIX REDUCED SELECT SERVICE UNION VALUES WHERE",
 "BOUND CONCAT COUNT DATATYPE IRI ISBLANK ISIRI ISLITERAL LANG MAX MIN REGEX STR STRLEN SUM URI",
 "#", "", "", false, "{", "}", ". ; , ( ) [ ] { } ?"},

{"sshconfig", "SSH Config", "sshconfig",
 "Host Match Include",
 "AddKeysToAgent AddressFamily BatchMode BindAddress Compression ConnectTimeout ControlMaster ControlPath ForwardAgent ForwardX11 HostName IdentityFile LocalForward LogLevel Port ProxyCommand ProxyJump RemoteForward ServerAliveInterval StrictHostKeyChecking User UserKnownHostsFile",
 "#", "", "", false, "", "", ""},

{"squirrel", "Squirrel", "nut",
 "base break case catch class clone const constructor continue default delete else enum extends false for foreach function if in instanceof local null resume return static switch this throw true try typeof while yield",
 "array compilestring print format getroottable setroottable rand srand type",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"standardml", "Standard ML", "sml sig fun",
 "abstype and andalso as case datatype do else end eqtype exception fn fun functor handle if in include infix infixr let local nonfix of op open orelse raise rec sharing sig signature struct structure then type val where while with withtype",
 "bool char int list option order real ref string unit vector",
 "", "(*", "*)", true, "", "", "+ - * / = <> < > <= >= := :: @ ^ -> => | _ ( ) [ ] { } , ;"},

{"stata", "Stata", "do ado stata",
 "break by capture continue display else exit foreach forvalues if in local macro program quietly return scalar set sort tempfile tempname tempvar use while",
 "collapse describe drop egen gen generate graph keep label list merge regress replace reshape save summarize tabulate",
 "//", "/*", "*/", true, "{", "}", kBasicOperators},

{"stylus", "Stylus", "styl",
 "@block @charset @css @extend @import @keyframes @media @support else for if return unless",
 "darken lighten rgba red green blue hue lightness saturation unit push pop",
 "//", "/*", "*/", false, "", "", kCOperators},

{"swift", "Swift", "swift",
 "associatedtype as async await break case catch class continue default defer deinit didSet do else enum extension fallthrough false fileprivate final for func get guard if import in indirect init inout internal is lazy let mutating nil nonmutating open operator private protocol public repeat rethrows return self Self set some static struct subscript super switch throw throws true try typealias var weak where while willSet",
 "Any AnyObject Array Bool Character Data Dictionary Double Error Float Int Int8 Int16 Int32 Int64 Never Optional Result Set String Substring UInt URL Void print",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── T ────────────────────────────────────────────────────────────────────────
{"textile", "Textile", "textile",
 "bq bc fn h1 h2 h3 h4 h5 h6 notextile p pre table",
 "",
 "", "", "", false, "", "", ""},

{"toml", "TOML", "toml",
 "true false inf nan",
 "",
 "#", "", "", true, "[", "]", "= . , [ ] { }"},

{"txt2tags", "txt2tags", "t2t",
 "includeconf includeurl include",
 "",
 "%", "", "", false, "", "", ""},

// ── U ────────────────────────────────────────────────────────────────────────
{"upc", "UPC", "upc",
 "auto break case char const continue default do double else enum extern float for goto if int long register relaxed return shared short signed sizeof static strict struct switch THREADS typedef union unsigned upc_forall void volatile while",
 "upc_barrier upc_fence upc_lock upc_notify upc_wait MYTHREAD",
 "//", "/*", "*/", true, "{", "}", kCOperators},

// ── V ────────────────────────────────────────────────────────────────────────
{"v", "V", "v vsh",
 "as asm assert atomic break const continue defer else enum false fn for go goto if import in interface is lock match module mut none or pub return rlock select shared sizeof static struct true type typeof union unsafe volatile",
 "any bool byte f32 f64 i8 i16 int i64 rune string u8 u16 u32 u64 voidptr println print eprintln",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"vala", "Vala", "vala vapi",
 "abstract as async base break case catch class const construct continue default delegate delete do dynamic else enum errordomain extern false finally for foreach get if in inline interface internal is lock namespace new null out override owned params private protected public ref requires return set signal sizeof static struct switch this throw throws true try typeof unowned using var virtual void weak while yield",
 "bool char double float int int8 int16 int32 int64 long size_t ssize_t string uchar uint uint8 uint16 uint32 uint64 ulong unichar",
 "//", "/*", "*/", true, "{", "}", kCOperators},

{"vb", "Visual Basic", "vb vbs bas frm cls ctl",
 "AddHandler AddressOf Alias And AndAlso As Boolean ByRef Byte ByVal Call Case Catch CBool CByte CDate CDbl CDec Char CInt Class CLng CObj Const Continue CSByte CShort CSng CStr CType CUInt CULng CUShort Date Decimal Declare Default Delegate Dim DirectCast Do Double Each Else ElseIf End EndIf Enum Erase Error Event Exit False Finally For Friend Function Get GetType GoTo Handles If Implements Imports In Inherits Integer Interface Is IsNot Let Lib Like Long Loop Me Mod Module MustInherit MustOverride MyBase MyClass Namespace Narrowing New Next Not Nothing NotInheritable NotOverridable Object Of On Operator Option Optional Or OrElse Overloads Overridable Overrides ParamArray Partial Preserve Private Property Protected Public RaiseEvent ReadOnly ReDim REM RemoveHandler Resume Return SByte Select Set Shadows Shared Short Single Static Step Stop String Structure Sub SyncLock Then Throw To True Try TryCast TypeOf UInteger ULong Until UShort Using Variant Wend When While Widening With WithEvents WriteOnly Xor",
 "Abs Array Asc Chr CInt Dir Format InStr IsNumeric LBound LCase Left Len Mid MsgBox Now Right Rnd Split Str Trim UBound UCase Val",
 "'", "", "", false, "", "", kBasicOperators},

{"velocity", "Velocity", "vm vtl",
 "#set #if #elseif #else #end #foreach #include #parse #macro #stop #break #define #evaluate",
 "$velocityCount $foreach $context $request $response $session",
 "##", "#*", "*#", false, "", "", kCOperators},

{"vim", "Vim Script", "vim vimrc",
 "au augroup autocmd break call catch command continue echo echoerr echohl echomsg else elseif endfor endfunction endif endtry endwhile execute finally finish for function if let return set setlocal silent source syntax throw try unlet while",
 "expand exists filereadable fnamemodify getline has hlexists join len map matchstr printf split strlen strpart substitute synIDattr type",
 "\"", "", "", true, "", "", kCOperators},

{"visualprolog", "Visual Prolog", "vpr pro",
 "and class clauses constants div domains end erroneous facts goal if implement inherits interface mod monitor namespace nondeterm open or predicates procedure resolve supports this try catch finally",
 "boolean char integer real string symbol unsigned",
 "%", "/*", "*/", true, "", "", kBasicOperators},

{"vue", "Vue", "vue",
 "template script style export default components props data computed methods watch setup emits v-if v-else v-else-if v-for v-bind v-on v-model v-show v-slot v-html v-text",
 "ref reactive computed watch onMounted onUnmounted defineProps defineEmits defineExpose nextTick",
 "//", "<!--", "-->", true, "", "", kCOperators},

// ── W ────────────────────────────────────────────────────────────────────────
{"whitespace", "Whitespace", "ws",
 "", "", "", "", "", true, "", "", ""},

{"winbatch", "WinBatch", "wbt",
 "break call continue else end exit for gosub goto if next return select switch then while",
 "AskLine Delay DirChange DisplayBox FileCopy FileDelete FileExist Message Run RunWait TerminateApp WinActivate WinExist",
 ";", "", "", false, "", "", kBasicOperators},

{"wml", "WML", "wml",
 "access card do go head meta noop onevent postfield prev refresh setvar template timer wml",
 "",
 "", "<!--", "-->", false, "", "", ""},

// ── X ────────────────────────────────────────────────────────────────────────
{"x10", "X10", "x10",
 "abstract as async at ateach athome atomic break case catch class clocked continue def do else extends extern false finally finish for goto has here if implements import in instanceof interface native new null offer offers operator package private property protected public return self static struct super switch this throw throws transient try true type val var when while",
 "Boolean Byte Char Double Float Int Long Object Point Region Short String UByte UInt ULong UShort",
 "//", "/*", "*/", true, "{", "}", kCOperators},
};

QSet<QString> toSet(const char *words)
{
    QSet<QString> out;
    if (!words || !*words)
        return out;
    const QString s = QString::fromLatin1(words);
    const QStringList parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &p : parts)
        out.insert(p);
    return out;
}

const Spec *findSpec(const QString &key)
{
    const QString k = key.toLower();
    for (const Spec &s : kSpecs)
        if (k == QLatin1String(s.key))
            return &s;
    return nullptr;
}

}  // namespace

const QVector<BuiltinLanguageEntry> &BuiltinLanguages::entries()
{
    static const QVector<BuiltinLanguageEntry> list = [] {
        QVector<BuiltinLanguageEntry> v;
        v.reserve(static_cast<int>(std::size(kSpecs)));
        for (const Spec &s : kSpecs) {
            BuiltinLanguageEntry e;
            e.key = QString::fromLatin1(s.key);
            e.display = QString::fromLatin1(s.display);
            e.extensions = QString::fromLatin1(s.exts)
                               .split(QLatin1Char(' '), Qt::SkipEmptyParts);
            v.push_back(e);
        }
        return v;
    }();
    return list;
}

bool BuiltinLanguages::contains(const QString &key)
{
    return findSpec(key) != nullptr;
}

QString BuiltinLanguages::keyForSuffix(const QString &suffix)
{
    static const QHash<QString, QString> index = [] {
        QHash<QString, QString> m;
        for (const auto &e : entries())
            for (const QString &ext : e.extensions)
                if (!m.contains(ext))   // 先登記者優先，避免副檔名互搶
                    m.insert(ext, e.key);
        return m;
    }();
    QString s = suffix.toLower();
    if (s.startsWith(QLatin1Char('.')))
        s = s.mid(1);
    return index.value(s);
}

UdlDefinition BuiltinLanguages::definitionFor(const QString &key)
{
    const Spec *s = findSpec(key);
    if (!s)
        return UdlDefinition();

    UdlDefinition d;
    d.name = QString::fromLatin1(s->display);
    d.extensions = QString::fromLatin1(s->exts).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    d.caseSensitive = s->caseSensitive;
    d.lineComment = QString::fromLatin1(s->lineComment);
    d.blockCommentStart = QString::fromLatin1(s->blockStart);
    d.blockCommentEnd = QString::fromLatin1(s->blockEnd);
    d.operators = toSet(s->operators);

    const QSet<QString> g0 = toSet(s->kw1);
    const QSet<QString> g1 = toSet(s->kw2);
    d.keywords = g0;                 // 向後相容欄位
    d.keywordGroups.push_back(g0);
    if (!g1.isEmpty())
        d.keywordGroups.push_back(g1);
    d.keywordGroupPrefixMode = QVector<bool>(d.keywordGroups.size(), false);

    d.folderTokens.open = QString::fromLatin1(s->foldOpen);
    d.folderTokens.close = QString::fromLatin1(s->foldClose);

    return d;
}

}  // namespace macpad::features
