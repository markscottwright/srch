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

string tolower(string const& str) {
    string lower_str;
    lower_str.resize(str.size());
    transform(
        str.begin(), str.end(), lower_str.begin(),
        [](unsigned char i) { return tolower(i); });
    return lower_str;
}

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

    directory_entry const* operator->() {
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

int main(int argc, char* argv[])
{
    try {
        auto exclude_directories = set<string> {".git", "__pycache__"};
        srch_directory_iterator end2;
        for (auto it = srch_directory_iterator(".", exclude_directories); it !=
                end2; ++it) {
            cout << it->path() << endl;
        }
        auto it = begin(srch_directory_iterator(".", exclude_directories));
        auto e = end(srch_directory_iterator(".", exclude_directories));
        for (auto p : srch_directory_iterator(".", exclude_directories))
            ;
    }
    catch (exception& e) {
        cerr << e.what() << endl;
    }
    return 0;
}


