#!/usr/bin/env python3
''' A tool for analyzing and allow correcting coding style of specified
source code on command line

This tool analyzes the C files specified on the command line using
the tool "clang-format"

The tool outputs the result of this analysis for each file. In-place
correction can be done by using dedicated options

The tool exits with a status of non-zero on any spotted error, making
this useful for checking PRs and can thus be added to travis.

As a best practice, this tool should always be run prior submitting a PR
'''

import argparse
import os
import sys
import subprocess

SOURCE_DIR = os.path.realpath(os.path.join(os.path.abspath(__file__), "../../.."))

CLANG_CONFIG_FILENAME = os.path.join(SOURCE_DIR, ".clang-format")

CLANG_FORMAT_CONFIG = "\
Language:        Cpp\n\
AccessModifierOffset: -1\n\
AlignAfterOpenBracket: Align\n\
AlignConsecutiveAssignments: false\n\
AlignConsecutiveDeclarations: false\n\
AlignEscapedNewlines: Left\n\
AlignOperands:   true\n\
AlignTrailingComments: true\n\
AllowAllParametersOfDeclarationOnNextLine: true\n\
AllowShortBlocksOnASingleLine: false\n\
AllowShortCaseLabelsOnASingleLine: false\n\
AllowShortFunctionsOnASingleLine: None\n\
AllowShortIfStatementsOnASingleLine: false\n\
AllowShortLoopsOnASingleLine: false\n\
AlwaysBreakAfterDefinitionReturnType: None\n\
AlwaysBreakAfterReturnType: None\n\
AlwaysBreakBeforeMultilineStrings: false\n\
AlwaysBreakTemplateDeclarations: true\n\
BinPackArguments: true\n\
BinPackParameters: true\n\
BreakBeforeBraces: Custom\n\
BraceWrapping:\n\
    AfterClass:      false\n\
    AfterControlStatement: false\n\
    AfterEnum:       false\n\
    AfterFunction:   false\n\
    AfterNamespace:  false\n\
    AfterObjCDeclaration: false\n\
    AfterStruct:     false\n\
    AfterUnion:      false\n\
    AfterExternBlock: false\n\
    BeforeCatch:     false\n\
    BeforeElse:      false\n\
    IndentBraces:    false\n\
    SplitEmptyFunction: true\n\
    SplitEmptyRecord: true\n\
    SplitEmptyNamespace: true\n\
BreakAfterJavaFieldAnnotations: false\n\
BreakBeforeBinaryOperators: None\n\
BreakBeforeBraces: Attach\n\
BreakBeforeInheritanceComma: false\n\
BreakBeforeTernaryOperators: false\n\
BreakConstructorInitializersBeforeComma: false\n\
BreakConstructorInitializers: AfterColon\n\
BreakStringLiterals: true\n\
ColumnLimit:     80\n\
CommentPragmas:  '^ IWYU pragma:'\n\
CompactNamespaces: false\n\
ConstructorInitializerAllOnOneLineOrOnePerLine: false\n\
ConstructorInitializerIndentWidth: 4\n\
ContinuationIndentWidth: 4\n\
Cpp11BracedListStyle: false\n\
DerivePointerAlignment: false\n\
DisableFormat:   false\n\
ExperimentalAutoDetectBinPacking: false\n\
FixNamespaceComments: true\n\
IncludeBlocks:   Preserve\n\
IncludeCategories:\n\
    - Regex:           '^<ext/.*\.h>'\n\
      Priority:        2\n\
    - Regex:           '^<.*\.h>'\n\
      Priority:        1\n\
    - Regex:           '^<.*'\n\
      Priority:        2\n\
    - Regex:           '.*'\n\
      Priority:        3\n\
IncludeIsMainRegex: '([-_](test|unittest))?$'\n\
IndentCaseLabels: false\n\
IndentPPDirectives: None\n\
IndentWidth:     4\n\
IndentWrappedFunctionNames: false\n\
JavaScriptQuotes: Leave\n\
JavaScriptWrapImports: true\n\
KeepEmptyLinesAtTheStartOfBlocks: false\n\
MacroBlockBegin: ''\n\
MacroBlockEnd:   ''\n\
MaxEmptyLinesToKeep: 1\n\
NamespaceIndentation: All\n\
ObjCBlockIndentWidth: 2\n\
ObjCSpaceAfterProperty: false\n\
ObjCSpaceBeforeProtocolList: true\n\
PenaltyBreakAssignment: 20000\n\
PenaltyBreakBeforeFirstCallParameter: 10000000\n\
PenaltyBreakComment: 300\n\
PenaltyBreakFirstLessLess: 120\n\
PenaltyBreakString: 1000\n\
PenaltyExcessCharacter: 10000\n\
PenaltyReturnTypeOnItsOwnLine: 2000000000\n\
PointerAlignment: Right\n\
RawStringFormats: \n\
  - Language: Cpp\n\
    BasedOnStyle:    google\n\
ReflowComments:  true\n\
SortIncludes:    true\n\
SortUsingDeclarations: true\n\
SpaceAfterCStyleCast: true\n\
SpaceAfterTemplateKeyword: true\n\
SpaceBeforeAssignmentOperators: true\n\
SpaceBeforeParens: ControlStatements\n\
SpaceInEmptyParentheses: false\n\
SpacesBeforeTrailingComments: 2\n\
SpacesInAngles:  false\n\
SpacesInContainerLiterals: false\n\
SpacesInCStyleCastParentheses: false\n\
SpacesInParentheses: false\n\
SpacesInSquareBrackets: false\n\
Standard:        Auto\n\
TabWidth:        8\n\
UseTab:          Never\n"

def log(is_ok, message):
    '''
    Print test messages. This function will detect if current
    process is attached to a TTY and will output in colors
    if it's the case.
    '''
    if sys.stdout.isatty():
        text = "\033[1;31mFAIL" if not is_ok else "\033[1;32mPASS"
        print("\033[1m%s\033[0m: %s" % (text, message))
    else:
        text = "FAIL" if not is_ok else "PASS"
        print("%s: %s" % (text, message))

def run_clang_format(clang_format_bin, sources, correct):
    '''
    Run clang format on a list of files and may perform
    inplace corrections if requested
    @return 0 if no error met while checking format.
    @return 0 if in correction mode
    '''
    count = 0
    passed = 0
    failed = False
    if not correct:
        cmd = [clang_format_bin, "-style=file",
               "-output-replacements-xml"]
    else:
        cmd = [clang_format_bin, "-style=file",
               "-i"]
    try:
        for src in sources:
            checkcmd = cmd[:]
            checkcmd.append(src)
            process = subprocess.Popen(checkcmd,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       encoding="utf-8")
            (cmd_stdout, cmd_stderr) = process.communicate()
            if process.returncode != 0:
                log(False, "An error occured while analysing the file")
                log(False, "Error : %s" % cmd_stderr)
                failed = True
            else:
                if "replacement " in cmd_stdout:
                    log(False, "%s is not correctly formated" % src)
                    failed = True
                else:
                    log(True, "%s" % src)
                    passed += 1
            count += 1
    except subprocess.CalledProcessError as error:
        print("Error calling clang-format [{}]".format(error))
        return error.returncode
    except FileNotFoundError as error:
        print("Specified clang-format binary (%s) was not found" % (clang_format_bin))
        return 1

    if not correct:
        print("TEST RESULT : %s/%s" % (str(passed), str(count)))

    return 0 if not failed else 1

def formatcheck():
    '''
    Parse commande line arguments and call for format checking function
    '''
    parser = argparse.ArgumentParser(prog='formatcheck',
                                     description='Perform coding style checking on source code')
    parser.add_argument("sources", nargs='+',
                        help="List of file to analyse")
    parser.add_argument('--clang-format-bin', type=str, default="clang-format",
                        help="Allow chosing clang format binary")
    parser.add_argument('--correct', type=bool, default=False,
                        help="Allow correcting coding style for specified " + \
                             "files. Corrections will occur in-situ")
    args = parser.parse_args()

    return run_clang_format(args.clang_format_bin, args.sources, args.correct)

if __name__ == '__main__':
    RES = 1
    try:
        with open(os.path.join(SOURCE_DIR, ".clang-format"), "w", encoding="utf-8") as config:
            config.write(CLANG_FORMAT_CONFIG)
        RES = formatcheck()
    except KeyboardInterrupt:
        print("Script interrupted !")
        RES = 127

    try:
        os.remove(CLANG_CONFIG_FILENAME)
    except OSError as error:
        print("An error occured while removing config. file (%s)", error)

    exit(RES)
