#include <nlohmann/json.hpp>

#include <cstdlib>

#include <regex>

#include <iostream>

using nlohmann::json;

#define REGEX_NAMESPACE std

namespace validator
{

class error_handler
{
	bool error_{false};

public:
	void error(const std::string &path, const json &instance, const std::string &message)
	{
		std::cerr << "ERROR: '" << path << "' - '" << instance << "': " << message << "\n";
		error_ = true;
	}

	void reset() { error_ = false; }
	operator bool() const { return error_; }
};

class schema
{
public:
	virtual void validate(const json &instance, error_handler &e) const = 0;

	static std::shared_ptr<schema> make(const json &schema);
};

class type_schema : public schema
{
	std::vector<std::shared_ptr<schema>> type_;

	std::pair<bool, json> enum_;
	std::pair<bool, json> const_;

	static std::shared_ptr<schema> make(const json &schema, json::value_t type);

public:
	type_schema(const json &schema)
	    : type_((uint8_t) json::value_t::discarded + 1)
	{
		// association between JSON-schema-type and NLohmann-types
		static const std::vector<std::pair<std::string, json::value_t>> schema_types = {
		    {"null", json::value_t::null},
		    {"object", json::value_t::object},
		    {"array", json::value_t::array},
		    {"string", json::value_t::string},
		    {"boolean", json::value_t::boolean},
		    {"integer", json::value_t::number_integer},
		    {"integer", json::value_t::number_unsigned},
		    {"number", json::value_t::number_float},
		};
		auto attr = schema.find("type");

		if (attr == schema.end()) // no type field means all sub-types possible
			for (auto &t : schema_types)
				type_[(uint8_t) t.second] = type_schema::make(schema, t.second);
		else {
			switch (attr.value().type()) { // "type": "type"

			case json::value_t::string: {
				auto schema_type = attr.value().get<std::string>();
				for (auto &t : schema_types)
					if (t.first == schema_type)
						type_[(uint8_t) t.second] = type_schema::make(schema, t.second);
			} break;

			case json::value_t::array: // "type": ["type1", "type2"]
				for (const auto &schema_type : attr.value())
					for (auto &t : schema_types)
						if (t.first == schema_type)
							type_[(uint8_t) t.second] = type_schema::make(schema, t.second);
				break;
			default:
				break;
			}
		}

		// with nlohmann::json floats can be seen as unsigned or integer - reuse the number-validator for
		// integer values as well, if they have not been specified
		if (type_[(uint8_t) json::value_t::number_float] && !type_[(uint8_t) json::value_t::number_integer])
			type_[(uint8_t) json::value_t::number_integer] =
			    type_[(uint8_t) json::value_t::number_unsigned] =
			        type_[(uint8_t) json::value_t::number_float];

		attr = schema.find("enum");
		if (attr != schema.end())
			enum_ = {true, attr.value()};

		attr = schema.find("const");
		if (attr != schema.end())
			const_ = {true, attr.value()};
	}

	void validate(const json &instance, error_handler &e) const override final
	{
		// depending on the type of instance run the type specific validator - if present
		auto type = type_[(uint8_t) instance.type()];

		if (type)
			type->validate(instance, e);
		else
			e.error("", instance, "unexpected instance type");

		if (enum_.first) {
			bool seen_in_enum = false;
			for (auto &e : enum_.second)
				if (instance == e) {
					seen_in_enum = true;
					break;
				}

			if (!seen_in_enum)
				e.error("", instance, "instance not found in required enum");
		}

		if (const_.first &&
		    const_.second != instance)
			e.error("", instance, "instance not as required by const");
	}
};

class string : public schema
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

public:
	void validate(const json &instance, error_handler &e) const override
	{
		if (minLength_.first) {
			if (utf8_length(instance) < minLength_.second) {
				std::ostringstream s;
				s << "'" << instance << "' is too short as per minLength (" << minLength_.second << ")";
				e.error("", instance, s.str());
			}
		}

		if (maxLength_.first) {
			if (utf8_length(instance) > maxLength_.second) {
				std::ostringstream s;
				s << "'" << instance << "' is too long as per maxLength (" << maxLength_.second << ")";
				e.error("", instance, s.str());
			}
		}

#ifndef NO_STD_REGEX
		if (pattern_.first &&
		    !REGEX_NAMESPACE::regex_search(instance.get<std::string>(), pattern_.second))
			e.error("", instance, instance.get<std::string>() + " does not match regex pattern: " + patternString_);
#endif

		if (format_.first) {
			if (format_check_ == nullptr)
				e.error("", instance, std::string("A format checker was not provided but a format-attribute for this string is present. ") + " cannot be validated for " + format_.second);
			else
				format_check_(format_.second, instance);
		}
	}

	string(const json &schema)
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
class numeric : public schema
{
	std::pair<bool, T> maximum_{false, 0};
	std::pair<bool, T> minimum_{false, 0};

	bool exclusiveMaximum_ = false;
	bool exclusiveMinimum_ = false;

	std::pair<bool, json::number_float_t> multipleOf_{false, 0};

	// multipleOf - if the rest of the division is 0 -> OK
	bool violates_multiple_of(json::number_float_t x) const
	{
		json::number_integer_t n = static_cast<json::number_integer_t>(x / multipleOf_.second);
		double res = (x - n * multipleOf_.second);
		return fabs(res) > std::numeric_limits<json::number_float_t>::epsilon();
	}

	void validate(const json &instance, error_handler &e) const override
	{
		T value = instance; // conversion of json to value_type

		if (multipleOf_.first && value != 0) // zero is multiple of everything
			if (violates_multiple_of(value))
				e.error("", instance, "is not a multiple of " + std::to_string(multipleOf_.second));

		if (maximum_.first)
			if ((exclusiveMaximum_ && value >= maximum_.second) ||
			    value > maximum_.second)
				e.error("", instance, "exceeds maximum of " + std::to_string(maximum_.second));

		if (minimum_.first)
			if ((exclusiveMinimum_ && value <= minimum_.second) ||
			    value < minimum_.second)
				e.error("", instance, "is below minimum of " + std::to_string(minimum_.second));
	}

public:
	numeric(const json &schema)
	{
		auto v = schema.find("maximum");
		if (v != schema.end())
			maximum_ = {true, v.value()};

		v = schema.find("minimum");
		if (v != schema.end())
			minimum_ = {true, v.value()};

		v = schema.find("exclusiveMaximum");
		if (v != schema.end()) {
			exclusiveMaximum_ = true;
			maximum_ = {true, v.value()};
		}

		v = schema.find("exclusiveMinimum");
		if (v != schema.end()) {
			minimum_ = {true, v.value()};
			exclusiveMinimum_ = true;
		}

		v = schema.find("multipleOf");
		if (v != schema.end())
			multipleOf_ = {true, v.value()};
	}
};

class null : public schema
{
	void validate(const json &instance, error_handler &e) const override
	{
		if (!instance.is_null())
			e.error("", instance, "expected to be null");
	}

public:
	null(const json &) {}
};

class boolean_type : public schema
{
	void validate(const json &, error_handler &) const override
	{ }

public:
	boolean_type(const json &) { }
};


class boolean : public schema
{
	bool true_;
	void validate(const json &instance, error_handler &e) const override
	{
		if (!true_) { // false schema
			// empty array
			//switch (instance.type()) {
			//case json::value_t::array:
			//	if (instance.size() != 0) // valid false-schema
			//		e.error("", instance, "false-schema required empty array");
			//	return;
			//}

			e.error("", instance, "instance invalid as par false-schema");
		}
	}

public:
	boolean(const json &schema)
	    : true_(schema) {}
};



#if 0
class required : public type_schema
{
	std::vector<std::string> required_;

	void validate(const json &instance, error_handler &e) const override
	{
		for (auto &r : required_)
			if (instance.find(r) == instance.end())
				throw std::invalid_argument("required property '" + r + "' not found in object '");
	}

public:
	required(const std::vector<std::string> &r)
	    : required_(r) {}
};

class dependencies_validator
{
	std::map<std::string, std::shared_ptr<base>> dependencies_;

	void validate(const json &instance, error_handler &e) const
	{
		for (auto &dep : dependencies_) {
			auto prop = instance.find(dep.first);
			if (prop != instance.end()) // if dependency-property is present in instance
				dep.second->validate(instance, e);  // validate
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
				// dependencies_[dep.key()] = createValidator(dep.value());
				break;

			default:
				break;
			}
		}
	}
};
#endif

class object : public schema
{
	std::pair<bool, size_t> maxProperties_{false, 0};
	std::pair<bool, size_t> minProperties_{false, 0};
	std::vector<std::string> required_;

	std::map<std::string, std::shared_ptr<schema>> properties_;
#ifndef NO_STD_REGEX
	std::vector<std::pair<REGEX_NAMESPACE::regex, std::shared_ptr<schema>>> patternProperties_;
#endif
	std::shared_ptr<schema> additionalProperties_;

//	std::map<std::string, dependencies_validator> dependencies_;

	std::shared_ptr<schema> propertyNames_;

	void validate(const json &instance, error_handler &e) const override
	{
		if (maxProperties_.first && instance.size() > maxProperties_.second)
			e.error("", instance, "too many properties.");

		if (minProperties_.first && instance.size() < minProperties_.second)
			e.error("", instance, "too few properties.");

		for (auto &r : required_)
			if (instance.find(r) == instance.end())
				e.error("", instance, "required property '" + r + "' not found in object '");

		// for each property in instance
		for (auto &p : instance.items()) {
			if (propertyNames_)
				propertyNames_->validate(p.key(), e);

			bool a_prop_or_pattern_matched = false;
			auto schema_p = properties_.find(p.key());
			// check if it is in "properties"
			if (schema_p != properties_.end()) {
				a_prop_or_pattern_matched = true;
				schema_p->second->validate(p.value(), e);
			}

			// check all matching patternProperties
			for (auto &schema_pp : patternProperties_)
				if (REGEX_NAMESPACE::regex_search(p.key(), schema_pp.first)) {
					a_prop_or_pattern_matched = true;
					schema_pp.second->validate(p.value(), e);
				}
			// check additionalProperties as a last resort
			if (!a_prop_or_pattern_matched && additionalProperties_)
				additionalProperties_->validate(p.value(), e);
		}
	}
public:
	object(const json &schema)
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
						schema::make(prop.value())));
		}

#ifndef NO_STD_REGEX
		attr = schema.find("patternProperties");
		if (attr != schema.end()) {
			for (auto prop : attr.value().items())
				patternProperties_.push_back(
				    std::make_pair(
				        REGEX_NAMESPACE::regex(prop.key(), REGEX_NAMESPACE::regex::ECMAScript),
				        schema::make(prop.value())));
		}
#endif

		attr = schema.find("additionalProperties");
		if (attr != schema.end())
			additionalProperties_ = schema::make(attr.value());

		//attr = schema.find("dependencies");
		//if (attr != schema.end())
		//	for (auto &dep : attr.value().items())
		//		dependencies_.emplace(std::make_pair(dep.key(), dependencies_validator(dep.value())));

		attr = schema.find("propertyNames");
		if (attr != schema.end())
			propertyNames_ = schema::make(attr.value());
	}

};

class array : public schema
{
	std::pair<bool, size_t> maxItems_{false, 0};
	std::pair<bool, size_t> minItems_{false, 0};
	bool uniqueItems_ = false;

	std::shared_ptr<schema> items_schema_;

	std::vector<std::shared_ptr<schema>> items_;
	std::shared_ptr<schema> additionalItems_;

	std::shared_ptr<schema> contains_;

	void validate(const json &instance, error_handler &e) const override
	{
		if (maxItems_.first && instance.size() > maxItems_.second)
			e.error("", instance, "has too many items.");

		if (minItems_.first && instance.size() < minItems_.second)
			e.error("", instance, "has too few items.");

		if (uniqueItems_) {
			for (auto it = instance.cbegin(); it != instance.cend(); ++it) {
				auto v = std::find(it + 1, instance.end(), *it);
				if (v != instance.end())
					e.error("", instance, "items have to be unique for this array.");
			}
		}

		if (items_schema_)
			for (auto &i : instance)
				items_schema_->validate(i, e);
		else {
			auto item = items_.cbegin();
			for (auto &i : instance) {
				std::shared_ptr<schema> item_validator;
				if (item == items_.cend())
					item_validator = additionalItems_;
				else {
					item_validator = *item;
					item++;
				}

				if (!item_validator)
					break;

				item_validator->validate(i, e);
			}
		}

		if (contains_) {
			bool contained = false;
			for (auto &item : instance) {
				error_handler local_e;
				contains_->validate(item, local_e);
				if (!local_e) {
					contained = true;
					break;
				}
			}
			if (!contained)
				e.error("", instance, "array does not contain required element as per 'contains'");
		}
	}

public:
	array(const json &schema)
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
		if (attr != schema.end()) {
			if (attr.value().type() == json::value_t::array) {
				for (auto &subschema : attr.value())
					items_.push_back(schema::make(subschema));

				attr = schema.find("additionalItems");
				if (attr != schema.end())
					additionalItems_ = schema::make(attr.value());

			} else if (attr.value().type() == json::value_t::object ||
					   attr.value().type() == json::value_t::boolean)
				items_schema_ = schema::make(attr.value());
		}

		attr = schema.find("contains");
		if (attr != schema.end())
			contains_ = schema::make(attr.value());
	}
};

std::shared_ptr<schema> type_schema::make(const json &schema, json::value_t type)
{
	switch (type) {
	case json::value_t::null:
		return std::make_shared<null>(schema);
	case json::value_t::number_unsigned:
		return std::make_shared<numeric<json::number_unsigned_t>>(schema);
	case json::value_t::number_integer:
		return std::make_shared<numeric<json::number_integer_t>>(schema);
	case json::value_t::number_float:
		return std::make_shared<numeric<json::number_float_t>>(schema);
	case json::value_t::string:
		return std::make_shared<string>(schema);
	case json::value_t::boolean:
		return std::make_shared<boolean_type>(schema);
	case json::value_t::object:
		return std::make_shared<object>(schema);
	case json::value_t::array:
		return std::make_shared<array>(schema);

	case json::value_t::discarded: // not a real type - silence please
		break;
	}
	return nullptr;
}

std::shared_ptr<schema> schema::make(const json &schema)
{
	// boolean schema
	if (schema.type() == json::value_t::boolean) {
		return std::make_shared<boolean>(schema);
	} else { // TODO logical and conditional schema

		return std::make_shared<type_schema>(schema);
	}
}

class root_schema : schema
{
	std::shared_ptr<schema> root_;

public:
	void set_root_schema(const json &schema)
	{
		root_ = schema::make(schema);
	}

	void validate(const json &instance, error_handler &e) const final
	{
		if (root_)
			root_->validate(instance, e);
		else
			e.error("", "", "no root schema has yet been set.");
	}
};

#if 0
validator *createValidator(const json &schema)
{
	// first check whether the schema is true or false
	if (schema.type() == json::value_t::boolean) {
		if (schema == true)
			return new always_true_validator;
		else
			return new always_false_validator;
	}

	// assume schema is an object
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
			return new null_validator(schema);
		else if (type == "boolean")
			return new boolean_validator(schema);
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
#endif

} // namespace validator

//template<ErrorHandlingPolicy, Default

int main(void)
{
	json validation; // a validation case following the JSON-test-suite-schema

	try {
		std::cin >> validation;
	} catch (std::exception &e) {
		std::cout << e.what() << "\n";
		return EXIT_FAILURE;
	}

	size_t total_failed = 0,
	       total = 0;

	for (auto &test_group : validation) {
		size_t group_failed = 0,
		       group_total = 0;

		std::cout << "Testing Group " << test_group["description"] << "\n";

		const auto &schema = test_group["schema"];

		validator::root_schema validator; // loader, format_check);

		validator.set_root_schema(schema);

		for (auto &test_case : test_group["tests"]) {
			std::cout << "  Testing Case " << test_case["description"] << "\n";

			bool valid = true;

			validator::error_handler err;
			validator.validate(test_case["data"], err);

			if (err)
				valid = false;

			if (valid == test_case["valid"])
				std::cout << "      --> Test Case exited with " << valid << " as expected.\n";
			else {
				group_failed++;
				std::cout << "      --> Test Case exited with " << valid << " NOT expected.\n";
			}
			group_total++;
			std::cout << "\n";
		}
		total_failed += group_failed;
		total += group_total;
		std::cout << "Group RESULT: " << test_group["description"] << " "
		          << (group_total - group_failed) << " of " << group_total
		          << " have succeeded - " << group_failed << " failed\n";
		std::cout << "-------------\n";
	}

	std::cout << "Total RESULT: " << (total - total_failed) << " of " << total << " have succeeded - " << total_failed << " failed\n";

	return total_failed;
}
