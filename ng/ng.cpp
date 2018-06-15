
#include <nlohmann/json.hpp>

using nlohmann::json;

// multipleOf
// maximum
// exclusiveMaximum

// minimum
// exclusiveMinimum
//

class validator
{
public:
	virtual bool operator()(const json &value) = 0;

	void error(const std::string &s)
	{}
};

class numeric_validator : public validator
{
	bool maximumCheck_ = false;
	double maximumValue;

	bool minimumCheck_ = false;
	double minimumValue;

	bool exclusiveMaximum_ = false;
	bool exclusiveMinimum_ = false;

	bool multipleOfCheck_ = false;
    double multipleOfValue_;

public:
	numeric_validator(const json &numeric)
	{
		auto &v = schema.find("maximum");
		if (v != schema.end()) {
			maximumCheck_ = true;
			maximumValue_ = v.value();
		}

		v = schema.find("minimum");
		if (v != schema.end()) {
			minimumCheck_ = true;
			minimumValue_ = v.value();
		}

		v = schema.find("exclusiveMaximum");
		if (v != schema.end())
			exclusiveMaximum_ = v.value();

		v = schema.find("exclusiveMinimum");
		if (v != schema.end())
			exclusiveMinimum_ = v.value();

		v = schema.find("multipleOf");
		if (v != schema.end()) {
			multipleOfCheck_ = true;
			multipleOfValue_ = v.value();
		}
	}


	bool operator()(const json &value) override
	{
		switch (value.type()) {
		case json::value_t::number_unsigned:
		case json::value_t::number_integer:
		case json::value_t::number_float:
			break;

		default:
			return false;
		}

		auto raw = value.get<double>();

		if (exclusiveMaximum_)
			if (raw >= exclusiveMaximumValue_)
			return false;

		if (exclusiveMinimum_ && raw <= exclusiveMinimumValue_)
			return false;

		if (minimum_ && raw < minimumValue_)
			return false;

		if (maximum_ && raw > maximumValue_)
			return false;

		if (multipleOf_ && multipleOfValue_ != 0.0) {
			double v = value;
			v /= multipleOf.value().get<double>();
		}

		return true;

	}
};

//template<ErrorHandlingPolicy, Default

int main(void)
{


	if (multipleOf != schema.end()) {
		if (multipleOf.value().get<double>() != 0.0) {

		}
	}

	numeric_validator<

	return EXIT_SUCCESS;
}

