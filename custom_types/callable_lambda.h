#include "core/variant/callable.h"
#include "core/object/callable_method_pointer.h"
//#include "core/variant/callable_bind.h"
//#include "core/extension/gdextension_interface.h"
#include <utility>

namespace impl {

// Func traits
template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())>
{};

template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const>
{
    using result_type = ReturnType;
    using arg_tuple = std::tuple<Args...>;
    static constexpr auto arity = sizeof...(Args);
};


template<class T>
struct ExpandPack;

template<class ...Args>
struct ExpandPack<std::tuple<Args...>> {
    template<class L, std::size_t ...Is>
    _FORCE_INLINE_ static void call(L&& l,const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error, IndexSequence<Is...>) {
#ifdef DEBUG_ENABLED
        if ((size_t)p_argcount > sizeof...(Args)) {
            r_call_error.error = Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
            r_call_error.expected = (int32_t)sizeof...(Args);
            return;
        }

        if ((size_t)p_argcount < sizeof...(Args)) {
            r_call_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
            r_call_error.expected = (int32_t)sizeof...(Args);
            return;
        }
#endif
        r_call_error.error = Callable::CallError::CALL_OK;
        using result_t = typename function_traits<std::decay_t<L>>::result_type;

#ifdef DEBUG_METHODS_ENABLED
        if constexpr (std::is_void_v<result_t>) {
            l(VariantCasterAndValidate<Args>::cast(p_arguments, Is, r_call_error)...);
        } 
		else {
            r_return_value = l(VariantCasterAndValidate<Args>::cast(p_arguments, Is, r_call_error)...);
        }
#else
        if constexpr (std::is_void_v<result_t>) {
            l(VariantCaster<Args>::cast(*p_arguments[Is])...);
        } 
		else {
            r_return_value = l(VariantCaster<Args>::cast(*p_arguments[Is])...);
        }
#endif

        (void)(p_arguments); // Avoid warning.
    }
};

// CallableCustom
template<class Lambda>
class CallableCustomLambda : public CallableCustom {
	Lambda l;
	Object* instance;

public:
	CallableCustomLambda(Object* inst, Lambda&& new_l) : l(new_l), instance(inst) {};
	virtual ~CallableCustomLambda() = default;

	uint32_t hash() const override {
		return (intptr_t)this;
	}

	String get_as_text() const override {
		return "CallableCustomLambda";
	}

	virtual CompareEqualFunc get_compare_equal_func() const override {
		return [](const CallableCustom* a, const CallableCustom* b) {
			return a->hash() == b->hash();
		};
	}

	CompareLessFunc get_compare_less_func() const override {
		return [](const CallableCustom* a, const CallableCustom* b) {
			return a->hash() < b->hash();
		};
	}

	bool is_valid() const override {
		return instance != nullptr;
	}

	ObjectID get_object() const override {
		return ObjectID();
	}

	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, Callable::CallError &r_call_error) const override {
		using traits = function_traits<Lambda>;
		using E = ExpandPack<typename traits::arg_tuple>;
		E::call(l, p_arguments, p_argcount, r_return_value, r_call_error, BuildIndexSequence<traits::arity>());
	}
};
}

// Binding
template <class Lambda>
Callable create_custom_callable_lambda(Object* p_instance, Lambda&& l) {
	auto* ccl = memnew(impl::CallableCustomLambda(p_instance, std::forward<Lambda>(l)));
	return Callable(ccl);
}


