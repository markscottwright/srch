/*
 * Author:          Mark Wright (markscottwright@gmail.com)
 * Creation Date:   2015-07-19
 */
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <cassert>

using namespace std;
using namespace std::tr2::sys;

#ifdef _WIN32
    const bool is_windows = true;
#else
    const bool is_windows = false;
#endif

const vector<string> DEFAULT_INCLUDES = {".*"};
const vector<string> DEFAULT_EXCLUDES = {
    "\\.sw[a-z]$",
    "\\.gitignore$",
    "\\.obj$",
    "\\.exe$",
};
const vector<string> DEFAULT_EXCLUDED_DIRECTORIES = {
    "^\\.git$",
    "^__pycache__$",
};

map<string, vector<string>> language_definitions = {
    {"cpp",         {"\\.cpp$", "\\.c$", "\\.h$", "\\.hpp$"}},
    {"python",      {"\\.py$", "\\.pyw$"}},
    {"html",        {"\\.html$", "\\.css$"}},
};

/**
 * would be nice if the stdlib had this...
 */
string tolower(string const& str) {
    string lower_str;
    lower_str.reserve(str.size());
    transform(
        str.begin(), str.end(), back_inserter(lower_str),
        [](unsigned char i) { return tolower(i); });
    return lower_str;
}

/**
 * This would be another thing that the stdlib should have...
 */
string escape_regex(string const& regex)
{
    // see http://en.cppreference.com/w/cpp/regex/ecmascript
    static const string special_characters("^$\\.*+?()[]{}|");
    string escaped;
    escaped.reserve(regex.size());
    for (auto ch : regex) {
        if (special_characters.find(ch) != string::npos)
            escaped.push_back('\\');
        escaped.push_back(ch);
    }

    return escaped;
}

bool startswith(string const& s, string const& prefix) {
    if (s.size() >= prefix.size()) {
        for (int i=0; i < prefix.size(); ++i)
            if (s[i] != prefix[i])
                return false;
    }
    return true;
}

/**
 * does path match the file globs in patterns?
 */
bool matches_pattern(vector<regex> const& patterns, path const& path) {
    string path_str = path.leaf();
    for (auto pattern : patterns) {
        if (regex_search(path_str, pattern))
            return true;
    }
    return false;
}

/**
 * set presence helper
 */
bool in(string const& elem, set<string> const& elems) {
    return elems.find(elem) != end(elems);
}

/**
 * Implement the InputIterator interface, with recursive directory searching and
 * skipping of files and directories matching patterns
 */
class srch_directory_iterator {
private:
    vector<path> path_stack;
    vector<regex> excluded_directories;
    vector<regex> included_files;
    vector<regex> excluded_files;
    directory_iterator end_;
    directory_iterator current_pos;

    bool accepted_file(path const& p) {
        return matches_pattern(included_files, p.leaf())
            && !matches_pattern(excluded_files, p.leaf());
    }

    bool accepted_directory(path const& d) {
        return !matches_pattern(excluded_directories, d);
    }

    /** if current_pos points to a non-excluded file, do nothing.  Otherwise,
     * move to the next non-excluded file or end_
     */
    void accept_or_move() {
        while (current_pos != end_ || !path_stack.empty()) {

            if (current_pos == end_) {
                auto dir_path = path_stack.back();
                path_stack.pop_back();
                current_pos = directory_iterator(dir_path);
            }

            // maybe we popped an empty directory iterator off.  If so, just
            // loop
            if (current_pos == end_)
                continue;

            // file
            if (!is_directory(current_pos->path())) {
                if (accepted_file(current_pos->path())) {
                    break;
                }
                else {
                    current_pos++;
                }
            }

            // directory
            else {
                if (accepted_directory(current_pos->path())) {
                    path_stack.push_back(current_pos->path());
                }
                current_pos++;
            }
        }
    }

public:
    srch_directory_iterator(string const& root,
            vector<regex> const& excluded_directories_,
            vector<regex> const& included_files_,
            vector<regex> const& excluded_files_)
        : excluded_directories(excluded_directories_),
            included_files(included_files_),
            excluded_files(excluded_files_)
    {
        current_pos = directory_iterator(path(root));

        // maybe the iterator points to a directory or excluded file - move to
        // the first acceptable one
        accept_or_move();
    }

    // for end()
    srch_directory_iterator() {
    }

    directory_entry const& operator*() const {
        return *current_pos;
    }

    directory_entry const* operator->() const {
        return &(*current_pos);
    }

    srch_directory_iterator& operator++() {
        // we're already done iterating
        if (current_pos == end_ && path_stack.empty())
            return *this;

        current_pos++;
        accept_or_move();

        return *this;
    }

    friend bool operator==(
        const srch_directory_iterator& rhs,
        const srch_directory_iterator& lhs);
};


bool operator==(
    const srch_directory_iterator& rhs,
    const srch_directory_iterator& lhs)
{
    return (rhs.current_pos == rhs.end_
            && lhs.current_pos == lhs.end_)
        || (rhs.current_pos == lhs.current_pos 
            && rhs.path_stack == lhs.path_stack);
}

bool operator!=(
    const srch_directory_iterator& rhs,
    const srch_directory_iterator& lhs)
{
    return !(rhs == lhs);
}

/** TODO - set to beginning.  But the problem is that setting to beginning is
 * potentially expensive.  The Std directory_iterator is a bad design anyway -
 * you don't iterate over an iterator, you use an iterator to iterate over a
 * "collection".
 */
srch_directory_iterator& begin(srch_directory_iterator& i) {
    return i;
}

srch_directory_iterator end(srch_directory_iterator& i) {
    srch_directory_iterator end_;
    return end_;
}

struct options_t {
    bool invert                         = false;
    bool ignore_case                    = false;
    bool match_words                    = false;
    bool literal_match                  = false;
    bool filenames_only                 = false;
    bool no_filenames                   = false;
    bool count                          = false;
    bool dump_options                   = false;
    int lines_before                    = 0;
    int lines_after                     = 0;
    int no_pattern                      = 0;
    vector<string> included_files       = DEFAULT_INCLUDES;
    vector<string> excluded_files       = DEFAULT_EXCLUDES;
    vector<string> excluded_directories = DEFAULT_EXCLUDED_DIRECTORIES;

    string join(vector<string> const& patterns) {
        const char* file_separator = is_windows ? ";" : ":";
        ostringstream joined;
        bool first_element = true;
        for (auto pattern : patterns) {
            if (first_element)
                first_element = false;
            else
                joined << file_separator;
            joined << pattern;
        }
        return joined.str();
    }

    void dump(ostream& out) {
        out << "==================================" << endl
            << "options" << endl
            << "==================================" << endl
            << "invert               = " << invert << endl
            << "ignore_case          = " << ignore_case << endl
            << "match_words          = " << match_words << endl
            << "literal_match        = " << literal_match << endl
            << "filenames_only       = " << filenames_only << endl
            << "no_filenames         = " << no_filenames << endl
            << "no-pattern           = " << no_pattern << endl
            << "count                = " << count << endl
            << "lines_before         = " << lines_before << endl
            << "lines_after          = " << lines_after << endl
            << "included_files       = " << join(included_files) << endl
            << "excluded_files       = " << join(excluded_files) << endl
            << "excluded_directories = " << join(excluded_directories) << endl
            << "==================================" << endl;
    }
};

void bounded_add(vector<string>& items, string const& item, size_t max_size)
{
    if (max_size > 0) {
        items.push_back(item);
        if (items.size() > max_size)
            items.erase(items.begin());
    }
}

/*
 * Strip the leading './' if present.
 */
string fixup(string const& path_str) {
    if (startswith(path_str, "./") || startswith(path_str, ".\\"))
        return string(begin(path_str)+2, end(path_str));
    return path_str;
}

string fixup(directory_entry const& path)
{
    return fixup(path.path());
}

void print_line(path const& file, int line_number, string const& line,
        bool no_filenames)
{
    if (!no_filenames)
        cout << fixup(file) << ":" << line_number << ":";
    cout << line << endl;
}

/**
 * parse options from command line, env variables and .srchrc.  Returns true if
 * parsing failed and we should print usage
 */
bool parse_options(
    int argc,
    char* argv[],
    options_t& options,
    vector<string>& patterns
    )
{
    bool print_usage = false;
    bool no_language_selected = true;
    for (int arg_pos = 1; arg_pos < argc; ++arg_pos) {
        string arg(argv[arg_pos]);
        if (in(arg, set<string>{"-i", "--ignore-case"}))
            options.ignore_case = true;
        else if (in(arg, set<string>{"-v", "--invert-match"}))
            options.invert = true;
        else if (in(arg, set<string>{"-w", "--word-regexp"}))
            options.match_words = true;
        else if (in(arg, set<string>{"-Q", "--literal"}))
            options.literal_match = true;
        else if (in(arg, set<string>{"-l", "--files-with-match"}))
            options.filenames_only = true;
        else if (in(arg, set<string>{"-L", "--files-without-match"})) {
            options.filenames_only = true;
            options.invert = true;
        }
        else if (in(arg, set<string>{"-h", "--no-filename"})) {
            options.no_filenames = true;
        }
        else if (in(arg, set<string>{"-f"})) {
            options.no_pattern = true;
        }
        else if (in(arg, set<string>{"-c", "--count"})) {
            options.count = true;
        }
        else if (in(arg, set<string>{"--dump-options"})) {
            options.dump_options = true;
        }
        else if (in(arg, set<string>{"-A", "--after-context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_after = atoi(arg.c_str());
        }
        else if (in(arg, set<string>{"-B", "--before-context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_before = atoi(arg.c_str());
        }
        else if (in(arg, set<string>{"-C", "--context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_after = options.lines_before = atoi(arg.c_str());
        }
        else if (in(arg, set<string>{"--help"})) {
            return false;
        }
        else if (!startswith(arg, "-")) {
            patterns.push_back(arg);
        }
        else if (startswith(arg, "--no")) {
            auto language_name = arg.substr(strlen("--no"));
            auto language_definition = language_definitions.find(language_name);
            if (language_definition == language_definitions.end()) {
                cout << "unknown language:" << arg << endl;
                return false;
            }
            copy(begin(language_definition->second),
                end(language_definition->second),
                back_inserter(options.excluded_files));
        }
        else if (startswith(arg, "--")) {
            auto language_name = arg.substr(strlen("--"));
            auto language_definition = language_definitions.find(language_name);
            if (language_definition == language_definitions.end()) {
                cout << "unknown language:" << arg << endl;
                return false;
            }
            else if (no_language_selected) {
                // don't go with defaults
                options.included_files.clear();
                no_language_selected = false;
            }
            copy(begin(language_definition->second),
                end(language_definition->second),
                back_inserter(options.included_files));
        }
        else {
            return false;
        }
    }

    return true;
}

string replace(const string& str, const string& pattern, const string& newval)
{
    assert(pattern != "");
    // seems as good a choice for an empty pattern as any...
    if (pattern == "")
        return str;

    string newstr;
    size_t last_end = 0, pattern_begin;
    while ((pattern_begin = str.find(pattern, last_end)) != string::npos) {
        newstr.append(str, last_end, pattern_begin-last_end);
        newstr.append(newval);
        last_end = pattern_begin + pattern.size();
    }
    newstr.append(str, last_end, str.size() - last_end);

    return newstr;
}

void print_usage(string program_name)
{
    static const vector<string> usage = {
"usage: program_name [options] PATTERN [files or directories]",
"",
"Search for PATTERN in each source file or in the tree from the",
"current directory down.",
"",
"Default switches may be specified in the SRCH_OPTIONS",
"environment variable or a .srchrc file.",
"",
"Example: program_name -i word",
"",
"Searching:",
"-i, --ignore-case          Ignore case distinctions in PATTERN",
"-v, --invert-match         Return only lines which don't match PATTERN",
"-w, --word-regexp          Only match if PATTERN is a word",
"-Q, --literal              Match PATTERN as literal value, not regexp",
"",
"Search output:",
"-l, --files-with-match     Print names of files that match PATTERN",
"-L, --files-without-match  Print names of files that do not match PATTERN",
"-h, --no-filename          Suppress printing of filename",
"-c, --count                Print number of lines that match pattern",
"--dump-options             Print options to program_name from command",
"                           line, environment variables and .srchrc",
"-A N, --after-context=N    Print N lines of input after matching line",
"-B N, --before-context=N   Print N lines of input before matching line",
"-C N, --context=N          Print N lines of input before and after matching",
"                           line",
"",
"File finding:",
"-f                         Only print filenames selected",
"--[TYPE]                   Select files of TYPE",
"--no[TYPE]                 Do no select files of TYPE",
"",
"Miscellaneous:",
"--help                     Print this message",
        };

    for (auto line : usage) {
        cout << replace(line, "program_name", program_name) << endl;
    }
}

bool line_matches(string const& line, vector<string> const& patterns,
        bool ignore_case)
{
    return any_of(begin(patterns), end(patterns),
        [&](const string& pattern) {
            if (ignore_case)
                // TODO inefficient - too many tolower calls
                return tolower(line).find(tolower(pattern)) != string::npos;
            else
                return line.find(pattern) != string::npos;
    });
}

bool line_matches(string const& line, vector<regex> const& patterns)
{
    return any_of(begin(patterns), end(patterns),
        [&](const regex& pattern) {
            return regex_search(line, pattern);});
}

/** print the lines before the match */
void print_pre_context(
        vector<string> const& lines_before, 
        directory_entry const& file_path,
        int line_number,
        bool no_filenames)
{
    int context_line_number = line_number - lines_before.size();
    for (auto line_before : lines_before) {
        print_line(file_path, context_line_number, line_before, no_filenames);
        context_line_number++;
    }
}

vector<regex> build_regexes(
    vector<string> const& patterns,
    bool ignore_case,
    bool match_words
    )
{
    vector<regex> regex_patterns;
    regex::flag_type flags = regex::ECMAScript;
    if (ignore_case)
        flags |= regex::icase;
    for (auto pattern : patterns) {
        if (match_words)
            regex_patterns.push_back(regex("\\b" + pattern + "\\b", flags));
        else
            regex_patterns.push_back(regex(pattern, flags));
    }
    return regex_patterns;
}

/**
 * Returns matches in file, if options.filenames_only not set.  Otherwise
 * returns 1 or 0
 */
int search_file(
    directory_entry const& file_path,
    vector<string> const& patterns,
    vector<regex> const& regex_patterns,
    options_t const& options
    )
{
    int line_number = 0;
    vector<string> lines_before;
    int lines_after_left = 0;
    int matches_in_file = 0;

    // start looping through the file line by line
    ifstream file(file_path.path());
    string line;
    while (getline(file, line)) {
        line_number++;

        // any of the patterns present?
        bool found = options.literal_match 
            ? line_matches(line, patterns, options.ignore_case)
            : line_matches(line, regex_patterns);

        if ((found && !options.invert) || (!found && options.invert)) {
            matches_in_file++;

            // if filenames only, don't print out the match, but we can
            // only break early if we're not counting the total matches
            if (options.filenames_only) {
                if (!options.count) {
                    cout << fixup(file_path) << endl;
                    break;
                }

                else
                    continue;
            }

            // print context, if requested
            if (options.lines_before > 0) {
                print_pre_context(lines_before, file_path, line_number,
                        options.no_filenames);
                lines_before.clear();
            }

            // print matching line
            print_line(
                file_path, line_number, line, options.no_filenames);
            lines_after_left = options.lines_after;
        }

        // print any trailing context
        else if (lines_after_left > 0) {
            print_line(
                file_path, line_number, line, options.no_filenames);
            lines_after_left--;
        }

        // only add to before contex if we didn't print it
        else {
            bounded_add(lines_before, line, options.lines_before);
        }
    }

    return matches_in_file;
}

int main(int argc, char* argv[])
{
    // parse command line
    options_t options;
    vector<string> patterns;
    vector<regex> regex_patterns;
    if (!parse_options(argc, argv, options, patterns)) {
        print_usage(argv[0]);
        exit(1);
    }

    if (options.dump_options) {
        options.dump(cout);
        exit(0);
    }

    if (patterns.size() == 0 && !options.no_pattern) {
        print_usage(argv[0]);
        exit(1);
    }

    // if we're not doing a literal match, build regex objects
    // TODO literal match + word-regexp isn't working
    if (!options.literal_match) {
        regex_patterns = build_regexes(patterns,
                options.ignore_case,
                options.match_words);
    }

    try {
        // convert from strings to regexes
        auto excluded_directories = build_regexes(
            options.excluded_directories, is_windows, false);
        auto included_files = build_regexes(options.included_files, is_windows, false);
        auto excluded_files = build_regexes(options.excluded_files, is_windows, false);

        // process matching files
        int total_matches = 0;
        for (auto file_path : srch_directory_iterator(
                    ".", excluded_directories, included_files, excluded_files)) {
            if (options.no_pattern) {
                cout << fixup(file_path) << endl;
                continue;
            }

            int matches = search_file(
                    file_path, patterns, regex_patterns, options);
            total_matches += matches;
            if (options.count)
                cout << fixup(file_path) << " " << matches << endl;
        }

        if (options.count)
            cout << "total " << total_matches << endl;

        return (total_matches > 0) ? 0 : 1;
    }

    catch (exception& e) {
        cerr << e.what() << endl;
        return 1;
    }
}


