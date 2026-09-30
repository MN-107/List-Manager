@echo off
setlocal enabledelayedexpansion

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH. Install MinGW/MSYS2.
    exit /b 1
)

if not exist list_manager.cpp (
    echo [ERROR] list_manager.cpp not found. It must be next to build.bat.
    exit /b 1
)

echo == Extracting embedded tests.cpp ==
set "SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=$env:SELF; $all=[System.IO.File]::ReadAllText($p,[System.Text.Encoding]::UTF8); $enc=New-Object System.Text.UTF8Encoding($false); $m=[regex]::Match($all,'(?s)=== tests\.cpp ===(?<c>.*?)=== END tests\.cpp ==='); if(-not $m.Success){Write-Error 'embedded tests.cpp not found'; exit 1}; [System.IO.File]::WriteAllText('tests.cpp',$m.Groups['c'].Value,$enc); exit 0"
if errorlevel 1 (
    echo [ERROR] Test source extraction failed.
    exit /b 1
)

echo == Building list_manager.exe ==
g++ -O2 -municode -mwindows list_manager.cpp -o list_manager.exe -static -luser32 -lgdi32
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo == Building tests.exe ==
g++ -O2 -DUNICODE -D_UNICODE tests.cpp -o tests.exe -static -luser32 -lgdi32 -Wno-unused-function
if errorlevel 1 (
    echo [ERROR] Test build failed.
    exit /b 1
)

echo == Running tests ==
tests.exe
set "TESTS_EXIT=!errorlevel!"
if exist tests.exe del /q tests.exe
if exist tests.cpp del /q tests.cpp

if not "!TESTS_EXIT!"=="0" (
    echo [ERROR] Tests failed.
    exit /b 1
)

echo == Build OK ==
exit /b 0

REM ----------------------------------------------------------------------
REM Embedded sources. Everything below is data, read by the powershell
REM command above; cmd never executes these lines.
REM ----------------------------------------------------------------------
=== tests.cpp ===
#include <iostream>
#include <string>
#include <vector>

#include "list_manager.cpp"

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__           \
                      << "  " #cond << "\n";                               \
        }                                                                  \
    } while (0)

static bool operator==(const std::vector<std::wstring>& a,
                       const std::vector<std::wstring>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

static void TestUtf8Roundtrip() {
    std::wstring src = L"Привет мир 123 abc ™→";
    CHECK(FromUtf8(ToUtf8(src)) == src);
    CHECK(ToUtf8(L"").empty());
    CHECK(FromUtf8("").empty());
}

static void TestParseBasic() {
    std::string content =
        "[LIST1]\n"
        "яблоко\n"
        "банан\n"
        "[LIST2]\n"
        "кошка\n"
        "собака\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK((lists[0].items == std::vector<std::wstring>{L"яблоко", L"банан"}));
    CHECK((lists[1].items == std::vector<std::wstring>{L"кошка", L"собака"}));
}

static void TestParseCrLf() {
    std::string content =
        "[LIST1]\r\n"
        "one\r\n"
        "two\r\n"
        "[LIST2]\r\n"
        "three\r\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK((lists[0].items == std::vector<std::wstring>{L"one", L"two"}));
    CHECK((lists[1].items == std::vector<std::wstring>{L"three"}));
}

static void TestParseIgnoresUnknown() {
    std::string content =
        "мусор до секции\n"
        "строка без секции\n"
        "[LIST1]\n"
        "aaa\n"
        "[OTHER]\n"
        "bbb\n"
        "[LIST2]\n"
        "ccc\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK((lists[0].items == std::vector<std::wstring>{L"aaa"}));
    CHECK((lists[1].items == std::vector<std::wstring>{L"ccc"}));
}

static void TestParseNoFinalNewline() {
    std::string content =
        "[LIST1]\n"
        "a\n"
        "[LIST2]\n"
        "b";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK((lists[0].items == std::vector<std::wstring>{L"a"}));
    CHECK((lists[1].items == std::vector<std::wstring>{L"b"}));
}

static void TestParseEmptyFile() {
    std::vector<ListStore> lists;
    ParseLists("", lists);
    CHECK(lists.empty());
}

static void TestParseMissingSection() {
    std::string content = "[LIST1]\nxyz\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 1);
    CHECK((lists[0].items == std::vector<std::wstring>{L"xyz"}));
}

static void TestSaveFormatMatchesParser() {
    std::wstring t1 = L"Продукты";
    std::wstring t2 = L"Люди";
    std::vector<std::wstring> src1 = {L"первый", L"второй", L""};
    std::vector<std::wstring> src2 = {L"third"};
    std::string content = "[TITLE1]\n";
    content += ToUtf8(t1) + "\n";
    content += "[TITLE2]\n";
    content += ToUtf8(t2) + "\n";
    content += "[LIST1]\n";
    for (const auto& it : src1) content += ToUtf8(it) + "\n";
    content += "[LIST2]\n";
    for (const auto& it : src2) content += ToUtf8(it) + "\n";

    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK(lists[0].title == t1);
    CHECK(lists[1].title == t2);
    CHECK(lists[0].items == src1);
    CHECK(lists[1].items == src2);
}

static void TestParseOldFormatNoTitles() {
    std::string content = "[LIST1]\na\n[LIST2]\nb\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists[0].title.empty());
    CHECK(lists[1].title.empty());
    CHECK((lists[0].items == std::vector<std::wstring>{L"a"}));
    CHECK((lists[1].items == std::vector<std::wstring>{L"b"}));
}

static void TestParseTwoThenFour() {
    std::string content =
        "[TITLE1]\n"
        "first\n"
        "[LIST1]\n"
        "a\n"
        "b\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 1);
    ParseLists(content + "[TITLE2]\nsecond\n[LIST2]\nc\n", lists);
    CHECK(lists.size() == 2);
    CHECK(lists[1].title == L"second");
}

static bool operator==(const std::vector<Tag>& a, const std::vector<Tag>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name) return false;
        if (a[i].indices != b[i].indices) return false;
    }
    return true;
}

static void TestParseTags() {
    std::string content =
        "[LIST1]\n"
        "яблоко\n"
        "банан\n"
        "вишня\n"
        "[LIST2]\n"
        "кошка\n"
        "[TAGS1]\n"
        "фрукт:0,2\n"
        "красный:0, 2\n"
        "[TAGS2]\n"
        "животное:0\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK(lists.size() == 2);
    CHECK((lists[0].tags == std::vector<Tag>{{L"фрукт", {0, 2}}, {L"красный", {0, 2}}}));
    CHECK(lists[0].tags[0].indices.size() == 2);
    CHECK((lists[1].tags == std::vector<Tag>{{L"животное", {0}}}));
}

static void TestParseTagsOutOfRange() {
    std::string content =
        "[LIST1]\n"
        "a\n"
        "b\n"
        "[TAGS1]\n"
        "x:1,2,5,abc,:,3:4\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK((lists[0].tags == std::vector<Tag>{{L"x", {1}}}));
}

static void TestParseTagRanges() {
    std::string content =
        "[LIST1]\n"
        "a\n"
        "b\n"
        "c\n"
        "d\n"
        "e\n"
        "f\n"
        "[TAGS1]\n"
        "range:0-3,5\n"
        "rev:4-2\n"
        "single:0\n";
    std::vector<ListStore> lists;
    ParseLists(content, lists);
    CHECK((lists[0].tags == std::vector<Tag>{{L"range", {0, 1, 2, 3, 5}},
                                             {L"rev", {2, 3, 4}},
                                             {L"single", {0}}}));
} 

static void TestWriteTagRanges() {
    ListStore st;
    st.items = {L"a", L"b", L"c", L"d", L"e", L"f"};
    st.tags = { {L"x", {1, 2, 3, 4}}, {L"y", {0, 2, 5, 6}} };
    std::ostringstream ss;
    WriteTags(ss, st);
    CHECK(ss.str() == "x:1-4\ny:0,2,5-6\n");
}

static void TestTagSuffix() {
    std::vector<std::wstring> v1 = {L"a", L"b", L"c"};
    std::vector<Tag> g1 = { {L"x", {0, 2}}, {L"y", {2}} };
    ListStore st;
    st.items = v1;
    st.tags = g1;
    CHECK(TagSuffix(st, 0) == L" (x)");
    CHECK(TagSuffix(st, 1) == L"");
    CHECK(TagSuffix(st, 2) == L" (x, y)");
}

static void TestSwapRows() {
    ListStore st;
    st.items = {L"a", L"b", L"c", L"d"};
    st.tags = { {L"x", {0, 2}}, {L"y", {3}} };
    SwapRows(st, 0, 1);
    CHECK((st.items == std::vector<std::wstring>{L"b", L"a", L"c", L"d"}));
    CHECK((st.tags[0].indices == std::vector<int>{1, 2}));
    CHECK((st.tags[1].indices == std::vector<int>{3}));
    SwapRows(st, 2, 3);
    CHECK((st.items == std::vector<std::wstring>{L"b", L"a", L"d", L"c"}));
    CHECK((st.tags[0].indices == std::vector<int>{1, 3}));
    CHECK((st.tags[1].indices == std::vector<int>{2}));
}

static void TestWriteReport() {
    ListStore st;
    st.items = {L"яблоко", L"банан", L"вишня"};
    st.tags = { {L"фрукт", {0, 2}}, {L"красный", {2}} };
    std::ostringstream ss;
    WriteReport(ss, L"мои фрукты", st);
    std::string expected =
        "\xd0\xbc\xd0\xbe\xd0\xb8 \xd1\x84\xd1\x80\xd1\x83\xd0\xba\xd1\x82\xd1\x8b\n"
        "\xd1\x8f\xd0\xb1\xd0\xbb\xd0\xbe\xd0\xba\xd0\xbe (\xd1\x84\xd1\x80\xd1\x83\xd0\xba\xd1\x82)\n"
        "\xd0\xb1\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbd\n"
        "\xd0\xb2\xd0\xb8\xd1\x88\xd0\xbd\xd1\x8f (\xd1\x84\xd1\x80\xd1\x83\xd0\xba\xd1\x82, \xd0\xba\xd1\x80\xd0\xb0\xd1\x81\xd0\xbd\xd1\x8b\xd0\xb9)\n";
    CHECK(ss.str() == expected);
    CHECK(TagSuffix(st, 0) == L" (фрукт)");
    CHECK(TagSuffix(st, 2) == L" (фрукт, красный)");
}

int main() {
    TestUtf8Roundtrip();
    TestParseBasic();
    TestParseCrLf();
    TestParseIgnoresUnknown();
    TestParseNoFinalNewline();
    TestParseEmptyFile();
    TestParseMissingSection();
TestSaveFormatMatchesParser();
    TestParseOldFormatNoTitles();
    TestParseTwoThenFour();
    TestParseTags();
    TestParseTagsOutOfRange();
    TestParseTagRanges();
    TestWriteTagRanges();
    TestTagSuffix();
    TestSwapRows();
    TestWriteReport();

    std::cout << (failures == 0 ? "OK  " : "FAIL") << " "
              << checks - failures << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
=== END tests.cpp ===
