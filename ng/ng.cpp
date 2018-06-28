#include <nlohmann/json.hpp>

#include <regex>

#include <iostream>

using nlohmann::json;

#define REGEX_NAMESPACE std

class validator
{
public:
	virtual void operator()(const json &value) const = 0;

	void error(const std::string &)
	{
	}
};

class type_validator : public validator
{
	json::value_t type_;

protected:
	void operator()(const json &instance) const override
	{
		if (instance.type() != type_)
			return;
	}

public:
	type_validator(json::value_t t)
	    : type_(t) {}
};

validator *createValidator(const json &schema);

class string_validator : public type_validator
{
	std::pair<bool, size_t> maxLength_{false, 0};
	std::pair<bool, size_t> minLength_{false, 0};

#ifndef NO_STD_REGEX
	std::pair<bool, REGEX_NAMESPACE::regex> pattern_{false, REGEX_NAMESPACE::regex()};
	std::string patternString_;
#endif

	std::pair<bool, std::string> format_;
	std::function<void(const std::string &, const std::string &)> format_check_ = nullptr;

	std::size_t utf8_length(const std::string &s) const
	{
		size_t len = 0;
		for (const unsigned char &c : s)
			if ((c & 0xc0) != 0x80)
				len++;
		return len;
	}

	void operator()(const json &instance) const override
	{
		type_validator::operator()(instance);

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

public:
	string_validator(const json &schema)
	    : type_validator(json::value_t::string)
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

		v = schema.find("format");
		if (v != schema.end())
			format_ = {true, v.value()};
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
	bool violates_multiple_of(json::number_float_t x) const
	{
		std::cerr << x << "\n";
		json::number_integer_t n = static_cast<json::number_integer_t>(x / multipleOf_.second);
		double res = (x - n * multipleOf_.second);
		return fabs(res) > std::numeric_limits<json::number_float_t>::epsilon();
	}

	void operator()(const json &instance) const override
	{
		T value = instance; // conversion of json to value_type

		if (multipleOf_.first && value != 0) // zero is multiple of everything
			if (violates_multiple_of(value))
				throw std::out_of_range("is not a multiple of " + std::to_string(multipleOf_.second));

		if (maximum_.first)
			if ((exclusiveMaximum_ && value >= maximum_.second) ||
			    value > maximum_.second)
				throw std::out_of_range("exceeds maximum of " + std::to_string(maximum_.second));

		if (minimum_.first)
			if ((exclusiveMinimum_ && value <= minimum_.second) ||
			    value < minimum_.second)
				throw std::out_of_range("is below minimum of " + std::to_string(minimum_.second));
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

class required_validator : public validator
{
	std::vector<std::string> required_;

	void operator()(const json &instance) const override
	{
		for (auto &r : required_)
			if (instance.find(r) == instance.end())
				throw std::invalid_argument("required property '" + r + "' not found in object '");
	}

public:
	required_validator(const std::vector<std::string> &r)
	    : required_(r) {}
};

class dependencies_validator : public validator
{
	std::map<std::string, validator *> dependencies_;

	void operator()(const json &instance) const override
	{
		for (auto &dep : dependencies_) {
			auto prop = instance.find(dep.first);
			if (prop != instance.end()) // if dependency-property is present in instance
				(*dep.second)(instance);  // validate
		}
	}

public:
	dependencies_validator(const json &object)
	{
		for (auto &dep : object.items()) {
			switch (dep.value().type()) {
			case json::value_t::array:
				dependencies_[dep.key()] = new required_validator(dep.value().get<std::vector<std::string>>());
				break;

			case json::value_t::object:
				dependencies_[dep.key()] = createValidator(dep.value());
				break;

			default:
				break;
			}
		}
	}
};

class object_validator : public type_validator
{
	std::pair<bool, size_t> maxProperties_{false, 0};
	std::pair<bool, size_t> minProperties_{false, 0};
	std::vector<std::string> required_;

	std::map<std::string, validator *> properties_;
#ifndef NO_STD_REGEX
	std::vector<std::pair<REGEX_NAMESPACE::regex, validator *>> patternProperties_;
#endif
	validator *additionalProperties_;

	std::map<std::string, dependencies_validator> dependencies_;

public:
	object_validator(const json &schema)
	    : type_validator(json::value_t::string)
	{
		auto attr = schema.find("maxProperties");
		if (attr != schema.end())
			maxProperties_ = {true, attr.value()};

		attr = schema.find("minProperties");
		if (attr != schema.end())
			minProperties_ = {true, attr.value()};

		attr = schema.find("required");
		if (attr != schema.end())
			required_ = attr.value().get<std::vector<std::string>>();

		attr = schema.find("properties");
		if (attr != schema.end()) {
			for (auto prop : attr.value().items())
				properties_.insert(
				    std::make_pair(
				        prop.key(),
				        createValidator(prop.value())));
		}

#ifndef NO_STD_REGEX
		attr = schema.find("patternProperties");
		if (attr != schema.end()) {
			for (auto prop : attr.value().items())
				patternProperties_.push_back(
				    std::make_pair(
				        REGEX_NAMESPACE::regex(prop.key(), REGEX_NAMESPACE::regex::ECMAScript),
				        createValidator(prop.value())));
		}
#endif

		attr = schema.find("additionalProperties");
		if (attr != schema.end())
			additionalProperties_ = createValidator(attr.value());

		attr = schema.find("dependencies");
		if (attr != schema.end())
			for (auto &dep : attr.value().items())
				dependencies_.emplace(std::make_pair(dep.key(), dependencies_validator(dep.value())));
	}

	void operator()(const json &instance) const override
	{
		type_validator::operator()(instance);

		if (maxProperties_.first && instance.size() > maxProperties_.second)
			throw std::out_of_range("too many properties.");

		if (minProperties_.first && instance.size() < minProperties_.second)
			throw std::out_of_range("too few properties.");

		for (auto &r : required_)
			if (instance.find(r) == instance.end())
				throw std::invalid_argument("required property '" + r + "' not found in object '");

		// for each property
		for (auto &p : instance.items()) {

			auto schema_p = properties_.find(p.key());
			// check if it is in "properties"
			if (schema_p != properties_.end())
				(*schema_p->second)(p.value());
			else {
				bool a_pattern_matched = false;
				// check all matching patternProperties
				for (auto &schema_pp : patternProperties_)
					if (REGEX_NAMESPACE::regex_search(p.key(), schema_pp.first)) {
						a_pattern_matched = true;
						(*schema_pp.second)(p.value());
					}
				// check additionalProperties as a last resort
				if (!a_pattern_matched && additionalProperties_)
					(*additionalProperties_)(p.value());
			}
		}
	}
};

class array_validator : public type_validator
{
	std::pair<bool, size_t> maxItems_{false, 0};
	std::pair<bool, size_t> minItems_{false, 0};
	bool uniqueItems_ = false;

	std::vector<validator *> items_;
	validator *additionalItems_ = nullptr;

	void operator()(const json &instance) const override
	{
		type_validator::operator()(instance);

		if (maxItems_.first && instance.size() > maxItems_.second)
			throw std::out_of_range("has too many items.");

		if (minItems_.first && instance.size() < minItems_.second)
			throw std::out_of_range("has too few items.");

		if (uniqueItems_) {
			for (auto it = instance.cbegin(); it != instance.cend(); ++it) {
				auto v = std::find(it + 1, instance.end(), *it);
				if (v != instance.end())
					throw std::out_of_range("items have to be unique for this array.");
			}
		}

		auto item = items_.cbegin();
		for (auto &i : instance) {
			validator *item_validator;
			if (item == items_.cend())
				item_validator = additionalItems_;
			else
				item_validator = *item;

			if (item_validator)
				break;

			(*item_validator)(i);

			item++;
		}
	}

public:
	array_validator(const json &schema)
	    : type_validator(json::value_t::array)
	{
		auto attr = schema.find("maxItems");
		if (attr != schema.end())
			maxItems_ = {true, attr.value()};

		attr = schema.find("minItems");
		if (attr != schema.end())
			minItems_ = {true, attr.value()};

		attr = schema.find("uniqueItems");
		if (attr != schema.end())
			uniqueItems_ = attr.value();

		attr = schema.find("items");
		if (attr != schema.end())
			for (auto &subschema : attr.value())
				items_.push_back(createValidator(subschema));

		attr = schema.find("additionalItems");
		if (attr != schema.end())
			additionalItems_ = createValidator(attr.value());

		// TODO contains
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
