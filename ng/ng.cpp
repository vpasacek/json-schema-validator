#include <nlohmann/json.hpp>

#include <regex>

#include <iostream>

using nlohmann::json;

#define REGEX_NAMESPACE std


class validator
{
public:
	virtual void operator()(const json &value) = 0;

	void error(const std::string &)
	{
	}
};

class string_validator : public validator
{
	std::pair<bool, size_t> maxLength_{false, 0};
	std::pair<bool, size_t> minLength_{false, 0};

#ifndef NO_STD_REGEX
	std::pair<bool, REGEX_NAMESPACE::regex> pattern_{false, REGEX_NAMESPACE::regex()};
	std::string patternString_;
#endif

	std::pair<bool, std::string> format_;
	std::function<void(const std::string &, const std::string &)> format_check_ = nullptr;

	std::size_t utf8_length(const std::string &s)
	{
		size_t len = 0;
		for (const unsigned char &c : s)
			if ((c & 0xc0) != 0x80)
				len++;
		return len;
	}

public:
	string_validator(const json &schema)
	{
		auto v = schema.find("maxLength");
		if (v != schema.end())
			maxLength_ = {true, v.value()};

		v = schema.find("minLength");
		if (v != schema.end())
			minLength_ = {true, v.value()};

#ifndef NO_STD_REGEX
		v = schema.find("pattern");
		if (v != schema.end()) {
			patternString_ = v.value();
			pattern_ = {true, REGEX_NAMESPACE::regex(v.value().get<std::string>(),
			                                         REGEX_NAMESPACE::regex::ECMAScript)};
		}
#endif

		// format
		v = schema.find("format");
		if (v != schema.end())
			format_ = {true, v.value()};
	}

	void operator()(const json &instance) override
	{
		switch (instance.type()) {
		case json::value_t::string:
			break;

		default:
			// throw
			return;
		}

		if (minLength_.first) {
			if (utf8_length(instance) < minLength_.second) {
				std::ostringstream s;
				s << "'" << instance << "' is too short as per minLength (" << minLength_.second << ")";
				throw std::out_of_range(s.str());
			}
		}

		if (maxLength_.first) {
			if (utf8_length(instance) > maxLength_.second) {
				std::ostringstream s;
				s << "'" << instance << "' is too long as per maxLength (" << maxLength_.second << ")";
				throw std::out_of_range(s.str());
			}
		}

#ifndef NO_STD_REGEX
		if (pattern_.first &&
			!REGEX_NAMESPACE::regex_search(instance.get<std::string>(), pattern_.second))
			throw std::invalid_argument(instance.get<std::string>() + " does not match regex pattern: " + patternString_);
#endif

		if (format_.first) {
			if (format_check_ == nullptr)
				throw std::logic_error(std::string("A format checker was not provided but a format-attribute for this string is present. ") +
				                       " cannot be validated for " + format_.second);
			else
				format_check_(format_.second, instance);
		}
	}
};

template <typename T>
class numeric_validator : public validator
{
	std::pair<bool, T> maximum_{false, 0};
	std::pair<bool, T> minimum_{false, 0};

	bool exclusiveMaximum_ = false;
	bool exclusiveMinimum_ = false;

	std::pair<bool, json::number_float_t> multipleOf_{false, 0};

	// multipleOf - if the rest of the division is 0 -> OK
	bool violates_multiple_of(json::number_float_t x)
	{
		std::cerr << x << "\n";
		json::number_integer_t n = static_cast<json::number_integer_t>(x / multipleOf_.second);
		double res = (x - n * multipleOf_.second);
		return fabs(res) > std::numeric_limits<json::number_float_t>::epsilon();
	}

public:
	numeric_validator(const json &schema)
	{
		auto v = schema.find("maximum");
		if (v != schema.end())
			maximum_ = {true, v.value()};

		v = schema.find("minimum");
		if (v != schema.end())
			minimum_ = {true, v.value()};

		v = schema.find("exclusiveMaximum");
		if (v != schema.end())
			exclusiveMaximum_ = v.value();

		v = schema.find("exclusiveMinimum");
		if (v != schema.end())
			exclusiveMinimum_ = v.value();

		v = schema.find("multipleOf");
		if (v != schema.end())
			multipleOf_ = {true, v.value()};
	}

	void operator()(const json &instance) override
	{
		T value = instance; // conversion of json to value_type

		if (multipleOf_.first && value != 0) { // zero is multiple of everything
			if (violates_multiple_of(value))
				throw std::out_of_range("is not a multiple of " + std::to_string(multipleOf_.second));
		}

		if (maximum_.first)
			if ((exclusiveMaximum_ && value >= maximum_.second) ||
			    value > maximum_.second)
				throw std::out_of_range("exceeds maximum of " + std::to_string(maximum_.second));

		if (minimum_.first)
			if ((exclusiveMinimum_ && value <= minimum_.second) ||
			    value < minimum_.second)
				throw std::out_of_range("is below minimum of " + std::to_string(minimum_.second));
	}
};

class type_validator : public validator
{
	json::value_t type_;
	void operator()(const json &instance) override
	{
		if (instance.type() != type_)
			return;
	}

public:
	type_validator(json::value_t t)
	    : type_(t) {}
};

class null_validator : public type_validator
{
public:
	null_validator()
	    : type_validator(json::value_t::null) {}
};

class boolean_validator : public type_validator
{
public:
	boolean_validator()
	    : type_validator(json::value_t::boolean) {}
};

class object_validator : public validator
{
	void operator()(const json &) override
	{
	}

public:
	object_validator(const json &)
	{
	}
};

class array_validator : public validator
{
	void operator()(const json &) override
	{
	}

public:
	array_validator(const json &)
	{
	}
};

validator *createValidator(const json &schema)
{
	json type;

	const auto type_iter = schema.find("type");
	if (type_iter == schema.end())
		type = "number";
	else
		type = type_iter.value();

	std::cerr << "type : " << type << "\n";

	switch (type.type()) {
	case json::value_t::array:
		// TODO handle array of strings
		break;

	case json::value_t::string:
		if (type == "string")
			return new string_validator(schema);
		else if (type == "null")
			return new null_validator;
		else if (type == "boolean")
			return new boolean_validator;
		else if (type == "object")
			return new object_validator(schema);
		else if (type == "array")
			return new array_validator(schema);
		else if (type == "number")
			return new numeric_validator<json::number_float_t>(schema);
		else if (type == "integer")
			return new numeric_validator<json::number_integer_t>(schema);

	default:
		// type field must be either an array or a string
		break;
	}

	return nullptr;
}

//template<ErrorHandlingPolicy, Default

int main(void)
{
	const json schema_minimum = R"({"minimum": 1.1})"_json;

	const json ok = 1.2;
	const json ko = 1.0;

	auto &validate = *createValidator(schema_minimum);

	std::cerr << "checking " << ok << "\n";
	try {
		validate(ok);
	} catch (std::exception &e) {
		std::cerr << "unexpected fail " << e.what() << "\n";
	}

	std::cerr << "checking " << ko << "\n";
	try {
		validate(ko);
	} catch (std::exception &e) {
		std::cerr << "expected fail " << e.what() << "\n";
	}

	return EXIT_SUCCESS;
}
