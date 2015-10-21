// Copyright 2015 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://sourceforge.net/projects/jsoncons/files/ for latest version
// See https://sourceforge.net/p/jsoncons/wiki/Home/ for documentation.

#ifndef JSONCONS_CSV_CSV_PARSER_HPP
#define JSONCONS_CSV_CSV_PARSER_HPP

#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <istream>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include "jsoncons/jsoncons.hpp"
#include "jsoncons/json_input_handler.hpp"
#include "jsoncons/parse_error_handler.hpp"
#include "jsoncons_ext/csv/csv_text_error_category.hpp"
#include "jsoncons_ext/csv/csv_parameters.hpp"

namespace jsoncons { namespace csv {

template <typename Char>
struct json_csv_parser_traits
{
};

template <>
struct json_csv_parser_traits<char>
{
    static const std::string string_literal() {return "string";};

    static const std::string integer_literal() {return "integer";};

    static const std::string float_literal() {return "float";};

    static const std::string boolean_literal() {return "boolean";};
};

template <>
struct json_csv_parser_traits<wchar_t> // assume utf16
{
    static const std::wstring string_literal() {return L"string";};

    static const std::wstring integer_literal() {return L"integer";};

    static const std::wstring float_literal() {return L"float";};

    static const std::wstring boolean_literal() {return L"boolean";};
};

namespace modes {
    enum modes_t {
        done,
        header,
		array,
		object
    };
};

namespace states {
    enum states_t {
        start, 
        comment,
        expect_value,
        between_fields,
		quoted_string,
		unquoted_string,
        escaped_value,
        minus, 
        zero,  
        integer,
        fraction,
        exp1,
        exp2,
        exp3,
        done
    };
};

namespace data_types
{
    enum column_types_t{string_t,integer_t,float_t,boolean_t};
};

template<typename Char>
class basic_csv_parser : private basic_parsing_context<Char>
{
    static const int default_depth = 3;

public:
    basic_csv_parser(basic_json_input_handler<Char>& handler)
       : top_(-1),
         stack_(default_depth),
         handler_(std::addressof(handler)),
         err_handler_(std::addressof(default_basic_parse_error_handler<Char>::instance())),
         is_negative_(false),
         cp_(0),
         index_(0)
    {
        depth_ = default_depth;
        state_ = states::start;
        top_ = -1;
        line_ = 1;
        column_ = 0;
        column_index_ = 0;
    }

    basic_csv_parser(basic_json_input_handler<Char>& handler,
                     basic_csv_parameters<Char> params)
       : top_(-1),
         stack_(default_depth),
         handler_(std::addressof(handler)),
         parameters_(params),
         err_handler_(std::addressof(default_basic_parse_error_handler<Char>::instance())),
         is_negative_(false),
         cp_(0),
         index_(0)

    {
        depth_ = default_depth;
        state_ = states::start;
        top_ = -1;
        line_ = 1;
        column_ = 0;
        column_index_ = 0;
    }

    basic_csv_parser(basic_json_input_handler<Char>& handler,
                     basic_parse_error_handler<Char>& err_handler)
       : top_(-1),
         stack_(default_depth),
         handler_(std::addressof(handler)),
         err_handler_(std::addressof(err_handler)),
         is_negative_(false),
         cp_(0),
         index_(0)

    {
        depth_ = default_depth;
        state_ = states::start;
        top_ = -1;
        line_ = 1;
        column_ = 0;
        column_index_ = 0;
    }

    basic_csv_parser(basic_json_input_handler<Char>& handler,
                     basic_parse_error_handler<Char>& err_handler,
                     basic_csv_parameters<Char> params)
       : top_(-1),
         stack_(default_depth),
         handler_(std::addressof(handler)),
         err_handler_(std::addressof(err_handler)),
         parameters_(params),
         is_negative_(false),
         cp_(0),
         index_(0)

    {
        depth_ = default_depth;
        state_ = states::start;
        top_ = -1;
        line_ = 1;
        column_ = 0;
        column_index_ = 0;
    }

    ~basic_csv_parser()
    {
    }

    const std::vector<std::basic_string<Char>>& column_labels() const
    {
        return column_labels_;
    }

    void column_labels(const std::vector<std::basic_string<Char>>& value)
    {
        column_labels_ = value;
    }

    basic_parsing_context<Char> const & parsing_context() const
    {
        return *this;
    }

    bool done() const
    {
        return state_ == states::done;
    }

    void after_field()
    {
        ++column_index_;
    }

    void before_record()
    {
        if (column_index_ == 0)
        {
            switch (stack_[top_])
            {
            case modes::array:
                handler_->begin_array(*this);
                break;
            case modes::object:
                handler_->begin_object(*this);
                break;
            }
        }
    }

    void after_record()
    {
        switch (stack_[top_])
        {
        case modes::array:
            handler_->end_array(*this);
            break;
        case modes::object:
            handler_->end_object(*this);
            break;
        case modes::header:
            if (line_ >= parameters_.header_lines())
            {
                if (column_labels_.size() > 0)
                {
                    flip(modes::header, modes::object);
                }
                else
                {
                    flip(modes::header, modes::array);
                }
            }
            break;
        }
        column_index_ = 0;
    }

    void begin_parse()
    {
        push(modes::done);
        handler_->begin_json();

        if (parameters_.header().length() > 0)
        {
            empty_basic_json_input_handler<Char> ih;
            basic_csv_parameters<Char> params;
            params.field_delimiter(parameters_.field_delimiter());
            params.quote_char(parameters_.quote_char());
            params.quote_escape_char(parameters_.quote_escape_char());
			params.assume_header(true);
            basic_csv_parser<Char> p(ih,params);
            p.begin_parse();
            p.parse(&(parameters_.header()[0]),0,parameters_.header().length());
            p.end_parse();
            column_labels(p.column_labels());
        }
        if (parameters_.data_types().length() > 0)
        {
            empty_basic_json_input_handler<Char> ih;
            basic_csv_parameters<Char> params;
            params.field_delimiter(parameters_.field_delimiter());
            params.assume_header(true);
            basic_csv_parser<Char> p(ih,params);
            p.begin_parse();
            p.parse(&(parameters_.data_types()[0]),0,parameters_.data_types().length());
            p.end_parse();
			column_types_.resize(p.column_labels().size());
            for (size_t i = 0; i < p.column_labels().size(); ++i)
            {
                if (p.column_labels()[i] == json_csv_parser_traits<Char>::string_literal())
                {
                    column_types_[i] = data_types::string_t;
                }
                else if (p.column_labels()[i] == json_csv_parser_traits<Char>::integer_literal())
                {
                    column_types_[i] = data_types::integer_t;
                }
                else if (p.column_labels()[i] == json_csv_parser_traits<Char>::float_literal())
                {
                    column_types_[i] = data_types::float_t;
                }
                else if (p.column_labels()[i] == json_csv_parser_traits<Char>::boolean_literal())
                {
                    column_types_[i] = data_types::boolean_t;
                }
            }
        }
        if (parameters_.header_lines() > 0)
        {
            push(modes::header);
        }
        else
        {
            push(modes::array);
        }
        handler_->begin_array(*this);
        state_ = states::expect_value;
        column_index_ = 0;
        prev_char_ = 0;
        column_ = 1;
    }

    void parse(Char const* p, size_t start, size_t length)
    {
        index_ = start;
        for (; index_ < length && state_ != states::done; ++index_)
        {
            int curr_char = p[index_];
all_states:
            switch (state_)
            {
            case states::comment:
                if (curr_char == '\n')
                {
                    state_ = states::expect_value;
                }
                else if (prev_char_ == '\r')
                {
                    state_ = states::expect_value;
                    goto all_states;
                }
                break;
            case states::expect_value:
                if (column_ == 1 && curr_char == parameters_.comment_starter())
                {
                    state_ = states::comment;
                }
                else
                {
                    state_ = states::unquoted_string;
                    goto all_states;
                }
                break;
            case states::between_fields:
                if (curr_char == '\r' || (prev_char_ != '\r' && curr_char == '\n'))
                {
                    after_record();
                    state_ = states::expect_value;
                }
                else if (curr_char == parameters_.field_delimiter())
                {
                    state_ = states::expect_value;
                }
                break;
            case states::escaped_value: 
                {
                    if (curr_char == parameters_.quote_char())
                    {
                        string_buffer_.push_back(curr_char);
                        state_ = states::quoted_string;
                    }
                    else if (parameters_.quote_escape_char() == parameters_.quote_char())
                    {
                        before_record();
                        end_string_value();
                        after_field();
                        state_ = states::between_fields;
                        goto all_states;
                    }
                }
                break;
            case states::quoted_string: 
                {
                    if (curr_char == parameters_.quote_escape_char())
                    {
                        state_ = states::escaped_value;
                    }
                    else if (curr_char == parameters_.quote_char())
                    {
                        before_record();
                        end_string_value();
                        after_field();
                        state_ = states::between_fields;
                    }
                    else
                    {
                        string_buffer_.push_back(curr_char);
                    }
                }
                break;
            case states::unquoted_string: 
                {
                    if (curr_char == '\r' || (prev_char_ != '\r' && curr_char == '\n'))
                    {
                        before_record();
                        end_string_value();
                        after_field();
                        after_record();
                        state_ = states::expect_value;
                    }
                    else if (curr_char == '\n')
                    {
                        if (prev_char_ != '\r')
                        {
                            before_record();
                            end_string_value();
                            after_field();
                            after_record();
                            state_ = states::expect_value;
                        }
                    }
                    else if (curr_char == parameters_.field_delimiter())
                    {
                        before_record();
                        end_string_value();
                        after_field();
                        state_ = states::expect_value;
                    }
                    else if (curr_char == parameters_.quote_char())
                    {
                        string_buffer_.clear();
                        state_ = states::quoted_string;
                    }
                    else
                    {
                        string_buffer_.push_back(curr_char);
                    }
                }
                break;
            default:
                err_handler_->error(std::error_code(csv_parser_errc::invalid_state, csv_text_error_category()), *this);
                break;
            }
            if (line_ > parameters_.max_lines())
            {
                state_ = states::done;
            }
            switch (curr_char)
            {
            case '\r':
                ++line_;
                column_ = 1;
                break;
            case '\n':
                if (prev_char_ != '\r')
                {
                    ++line_;
                }
                column_ = 1;
                break;
            default:
                ++column_;
                break;
            }
            prev_char_ = curr_char;
        }
    }

    void end_parse()
    {
        switch (state_)
        {
        case states::unquoted_string: 
            before_record();
            end_string_value();
            after_field();
            break;
        case states::escaped_value:
            if (parameters_.quote_escape_char() == parameters_.quote_char())
            {
                before_record();
                end_string_value();
                after_field();
            }
            break;
        }
        if (column_index_ > 0)
        {
            after_record();
        }
        switch (stack_[top_])
        {
        case modes::array:
            if (!pop(modes::array))
            {
                err_handler_->error(std::error_code(csv_parser_errc::unexpected_eof, csv_text_error_category()), *this);
            }
            break;
        case modes::object:
            if (!pop(modes::object))
            {
                err_handler_->error(std::error_code(csv_parser_errc::unexpected_eof, csv_text_error_category()), *this);
            }
            break;
        case modes::header:
            if (!pop(modes::header))
            {
                err_handler_->error(std::error_code(csv_parser_errc::unexpected_eof, csv_text_error_category()), *this);
            }
            break;
        }
        handler_->end_array(*this);
        if (!pop(modes::done))
        {
            err_handler_->error(std::error_code(csv_parser_errc::unexpected_eof, csv_text_error_category()), *this);
        }
        handler_->end_json();
    }

    states::states_t state() const
    {
        return state_;
    }

    size_t index() const
    {
        return index_;
    }
private:

    void end_string_value() 
    {
        switch (stack_[top_])
        {
        case modes::header:
            if (parameters_.assume_header() && line_ == 1)
            {
                column_labels_.push_back(string_buffer_);
            }
            state_ = states::expect_value;
            break;
        case modes::object:
            if (column_index_ < column_labels_.size())
            {
                handler_->name(column_labels_[column_index_].c_str(), column_labels_[column_index_].length(), *this);
            }
            else
            {
                handler_->name("", 0, *this);
            }
        case modes::array:
            if (column_index_ < column_types_.size())
            {
                switch (column_types_[column_index_])
                {
                case data_types::integer_t:
					{
						std::istringstream iss(string_buffer_);
						long long val;
						iss >> val;
						handler_->value(val, *this);
					}
                    break;
                case data_types::float_t:
                    {
                        std::istringstream iss(string_buffer_);
                        double val;
                        iss >> val;
                        handler_->value(val, *this);
                    }
                    break;
				default:
	                handler_->value(string_buffer_.c_str(), string_buffer_.length(), *this);
					break;	
                }
            }
            else
            {
                handler_->value(string_buffer_.c_str(), string_buffer_.length(), *this);
            }
            state_ = states::expect_value;
            break;
        default:
            err_handler_->error(std::error_code(csv_parser_errc::invalid_csv_text, csv_text_error_category()), *this);
            break;
        }
        string_buffer_.clear();
    }

    unsigned long do_line_number() const override
    {
        return line_;
    }

    unsigned long do_column_number() const override
    {
        return column_;
    }

    Char do_last_char() const override
    {
        return (Char)prev_char_;
    }

    void push(modes::modes_t modes)
    {
        ++top_;
        if (top_ >= depth_)
        {
            depth_ *= 2;
            stack_.resize(depth_);
        }
        stack_[top_] = modes;
    }

    int peek()
    {
        return stack_[top_];
    }

    bool peek(modes::modes_t modes)
    {
        return stack_[top_] == modes;
    }

    bool flip(modes::modes_t mode1, modes::modes_t mode2)
    {
        if (top_ < 0 || stack_[top_] != mode1)
        {
            return false;
        }
        stack_[top_] = mode2;
        return true;
    }

    bool pop(modes::modes_t modes)
    {
        if (top_ < 0 || stack_[top_] != modes)
        {
            return false;
        }
        --top_;
        return true;
    }

    template<typename CharType>
    unsigned long long string_to_unsigned(const CharType *s, size_t length) throw(std::overflow_error)
    {
        const unsigned long long max_value = std::numeric_limits<unsigned long long>::max JSONCONS_NO_MACRO_EXP();
        const unsigned long long max_value_div_10 = max_value / 10;
        unsigned long long n = 0;
        for (size_t i = 0; i < length; ++i)
        {
            unsigned long long x = s[i] - '0';
            if (n > max_value_div_10)
            {
                throw std::overflow_error("Unsigned overflow");
            }
            n = n * 10;
            if (n > max_value - x)
            {
                throw std::overflow_error("Unsigned overflow");
            }

            n += x;
        }
        return n;
    }

    template<typename CharType>
    long long string_to_integer(bool has_neg, const CharType *s, size_t length) throw(std::overflow_error)
    {
        const long long max_value = std::numeric_limits<long long>::max JSONCONS_NO_MACRO_EXP();
        const long long max_value_div_10 = max_value / 10;

        long long n = 0;
        for (size_t i = 0; i < length; ++i)
        {
            long long x = s[i] - '0';
            if (n > max_value_div_10)
            {
                throw std::overflow_error("Integer overflow");
            }
            n = n * 10;
            if (n > max_value - x)
            {
                throw std::overflow_error("Integer overflow");
            }

            n += x;
        }
        return has_neg ? -n : n;
    }

    states::states_t state_;
    int top_;
    std::vector<modes::modes_t> stack_;
    basic_json_input_handler<Char> *handler_;
    basic_parse_error_handler<Char> *err_handler_;
    unsigned long column_;
    unsigned long line_;
    int prev_char_;
    uint32_t cp_;
    uint32_t cp2_;
    std::basic_string<Char> string_buffer_;
    bool is_negative_;
    states::states_t saved_state_;
    size_t index_;
    int depth_;
    basic_csv_parameters<Char> parameters_;
    std::vector<std::basic_string<Char>> column_labels_;
    std::vector<data_types::column_types_t> column_types_;
	size_t column_index_;
};

typedef basic_csv_parser<char> csv_parser;
typedef basic_csv_parser<wchar_t> wcsv_parser;

}}

#endif

