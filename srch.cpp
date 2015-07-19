/* 
 * Author:          Mark Wright (wrightm@datacard.com)
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
 * does path match the file globs in patterns?
 */
bool matches_pattern(set<string> const& patterns, path const& path) {
    auto lower_path = tolower(path.leaf());
    return patterns.find(lower_path) != end(patterns);
}


class srch_directory_iterator {
private:
    vector<directory_iterator> path_stack;
    set<string> exclude_directories;
    directory_iterator end_;
    directory_iterator current_pos;

    bool accepted_file(path const& p) {
        return true;
    }

    bool accepted_directory(path const& d) {
        return !matches_pattern(exclude_directories, d);
    }

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
            set<string> const& exclude_directories_)
        : exclude_directories(exclude_directories_)
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
    bool filenames_only = true;
    bool no_filenames = false;
    bool count = false;
    int lines_before = 3;
    int lines_after = 0;
    set<string> included_files;
    set<string> excluded_files;
    set<string> included_directories;
    set<string> excluded_directories;
};

void bounded_add(vector<string>& items, string const& item, size_t max_size)
{
    if (max_size > 0) {
        items.push_back(item);
        if (items.size() > max_size)
            items.erase(items.begin());
    }
}

int main(int argc, char* argv[])
{
    string pattern = argv[1];
    options_t options;

    bool match_found = false;
    try {
        auto exclude_directories = set<string> {".git", "__pycache__"};
        for (auto file_path : srch_directory_iterator(".", exclude_directories)) {
            ifstream file(file_path.path());
            int line_number = 0;
            vector<string> lines_before;

            string line;
            while (getline(file, line)) {
                line_number++;
                bool found = (line.find(pattern) != string::npos);

                // for return code
                if (found || options.invert)
                    match_found = true;

                if ((found && !options.invert) || (!found && options.invert)) {

                    // print context, if requested
                    if (options.lines_before > 0) {
                        int context_line_number = line_number - lines_before.size();
                        for (auto line_before : lines_before) {
                            cout << file_path.path() << ":" << context_line_number << " " << line_before << endl;
                            context_line_number++;
                        }
                        lines_before.clear();
                    }

                    // print matching line
                    cout << file_path.path() << ":" << line_number << " " << line << endl;
                }

                // only add to before contex if we didn't print it
                else {
                    bounded_add(lines_before, line, options.lines_before);
                }
            }
        }
    }
    catch (exception& e) {
        cerr << e.what() << endl;
    }
    return match_found ? 0 : 1;
}


