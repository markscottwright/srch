/*
 * Author:          Mark Wright (markscottwright@gmail.com)
 * Creation Date:   2015-07-19
 */
#include <iostream>
#include <regex>
#include <set>
#include <filesystem>
#include <locale>
#include <algorithm>

using namespace std;
using namespace std::tr2::sys;

/**
 * would be nice if the stdlib had this...
 */
string tolower(string const& str) {
    string lower_str;
    lower_str.resize(str.size());
    transform(
        str.begin(), str.end(), lower_str.begin(),
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
 * does path match the regex in patterns?
 *
 * TODO globs?
 */
bool matches_pattern(vector<regex> const& patterns, path const& path) {
    for (regex const& pattern : patterns)
        if (regex_search(path.string(), pattern))
            return true;
    return false;
}

/**
 * set presence helper
 */
bool in(const char* elem, set<string> const& elems) {
    return elems.find(elem) != end(elems);
}

/**
 * Implement the InputIterator interface, with recursive directory searching and
 * skipping of files and directories matching patterns
 */
class srch_directory_iterator {
private:
    vector<directory_iterator> path_stack;
    vector<regex> included_files;
    vector<regex> excluded_files;
    vector<regex> excluded_directories;
    directory_iterator end_;
    directory_iterator current_pos;

    bool accepted_file(path const& p) {
        return matches_pattern(included_files, p)
            && !matches_pattern(excluded_files, p);
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
                current_pos = path_stack.back();
                path_stack.pop_back();
            }

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
                    path_stack.push_back(
                        directory_iterator(current_pos->path()));
                }
                current_pos++;
            }
        }
    }

public:
    srch_directory_iterator(string const& root,
            vector<regex> const& included_files_,
            vector<regex> const& excluded_files_,
            vector<regex> const& excluded_directories_)
        : included_files(included_files_),
            excluded_files(excluded_files_),
            excluded_directories(excluded_directories_)
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
    bool invert = false;
    bool ignore_case = false;
    bool match_words = false;
    bool literal_match = false;
    bool filenames_only = false;
    bool no_filenames = false;
    bool count = false;
    int lines_before = 0;
    int lines_after = 0;
    vector<string> included_files;
    vector<string> excluded_files;
    vector<string> excluded_directories;
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
 *
 * TODO: Convert to windows format if configured
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
    options.excluded_directories = vector<string> {"\\.git$", "__pycache__$"};
    options.excluded_files = vector<string> {
        "\\.gitignore$", "\\.exe$", "\\.obj$", "\\.swp$"};
    options.included_files = vector<string> {".*"};

    bool print_usage = false;
    for (int arg_pos = 1; arg_pos < argc; ++arg_pos) {
        if (in(argv[arg_pos], set<string>{"-i", "--ignore-case"}))
            options.ignore_case = true;
        else if (in(argv[arg_pos], set<string>{"-v", "--invert-match"}))
            options.invert = true;
        else if (in(argv[arg_pos], set<string>{"-w", "--word-regexp"}))
            options.match_words = true;
        else if (in(argv[arg_pos], set<string>{"-Q", "--literal"}))
            options.literal_match = true;
        else if (in(argv[arg_pos], set<string>{"-l", "--files-with-match"}))
            options.filenames_only = true;
        else if (in(argv[arg_pos], set<string>{"-L", "--files-without-match"})) {
            options.filenames_only = true;
            options.invert = true;
        }
        else if (in(argv[arg_pos], set<string>{"-h", "--no-filename"})) {
            options.no_filenames = true;
        }
        else if (in(argv[arg_pos], set<string>{"-c", "--count"})) {
            options.count = true;
        }
        else if (in(argv[arg_pos], set<string>{"-A", "--after-context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_after = atoi(argv[arg_pos]);
        }
        else if (in(argv[arg_pos], set<string>{"-B", "--before-context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_before = atoi(argv[arg_pos]);
        }
        else if (in(argv[arg_pos], set<string>{"-C", "--context"})) {
            arg_pos++;
            if (arg_pos >= argc)
                return false;
            options.lines_after = options.lines_before = atoi(argv[arg_pos]);
        }
        else if (in(argv[arg_pos], set<string>{"--help"})) {
            return false;
        }
        else if (!startswith(argv[arg_pos], "-")) {
            patterns.push_back(argv[arg_pos]);
        }
        else {
            return false;
        }
    }

    return true;
}

void print_usage(string program_name)
{
    cout << "usage:" << program_name << endl;
}

bool line_matches(string const& line, vector<string> const& patterns)
{
    for (auto pattern : patterns) {
        if (line.find(pattern) != string::npos) {
            return true;
        }
    }
    return false;
}

bool line_matches(string const& line, vector<regex> const& patterns)
{
    for (auto pattern : patterns) {
        if (regex_search(line, pattern)) {
            return true;
        }
    }
    return false;
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
    // TODO literal match + word-regexp isn't working
    // create regex patterns, if not doing literal match
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
            ? line_matches(line, patterns)
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

/* compile a list of regex strings */
static vector<regex> to_regex(vector<string> const& regex_strings)
{
    regex::flag_type flags = regex::ECMAScript;
#ifdef _WIN32
    flags |= regex::icase;
#endif
    
    vector<regex> regexes;
    for (auto regex_string : regex_strings)
        regexes.push_back(regex(regex_string, flags));
    return regexes;
}

int main(int argc, char* argv[])
{
    // parse command line
    options_t options;
    vector<string> patterns;
    vector<regex> regex_patterns;
    if (!parse_options(argc, argv, options, patterns)
            || patterns.size() == 0) {
        print_usage(argv[0]);
        exit(1);
    }

    // if we're not doing a literal match, build regex objects
    if (!options.literal_match) {
        regex_patterns = build_regexes(patterns,
                options.ignore_case,
                options.match_words);
    }

    try {
        int total_matches = 0;
        for (auto file_path :
                srch_directory_iterator(".",
                    to_regex(options.included_files),
                    to_regex(options.excluded_files),
                    to_regex(options.excluded_directories))) {
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


