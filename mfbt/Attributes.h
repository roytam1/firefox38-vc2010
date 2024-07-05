/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Implementations of various class and method modifier attributes. */

#ifndef mozilla_Attributes_h
#define mozilla_Attributes_h

#include "mozilla/Compiler.h"

/*
 * MOZ_ALWAYS_INLINE is a macro which expands to tell the compiler that the
 * method decorated with it must be inlined, even if the compiler thinks
 * otherwise.  This is only a (much) stronger version of the inline hint:
 * compilers are not guaranteed to respect it (although they're much more likely
 * to do so).
 *
 * The MOZ_ALWAYS_INLINE_EVEN_DEBUG macro is yet stronger. It tells the
 * compiler to inline even in DEBUG builds. It should be used very rarely.
 */
#if defined(_MSC_VER)
#  define MOZ_ALWAYS_INLINE_EVEN_DEBUG     __forceinline
#elif defined(__GNUC__)
#  define MOZ_ALWAYS_INLINE_EVEN_DEBUG     __attribute__((always_inline)) inline
#else
#  define MOZ_ALWAYS_INLINE_EVEN_DEBUG     inline
#endif

#if !defined(DEBUG)
#  define MOZ_ALWAYS_INLINE     MOZ_ALWAYS_INLINE_EVEN_DEBUG
#elif defined(_MSC_VER) && !defined(__cplusplus)
#  define MOZ_ALWAYS_INLINE     __inline
#else
#  define MOZ_ALWAYS_INLINE     inline
#endif

#if defined(_MSC_VER)
/*
 * g++ requires -std=c++0x or -std=gnu++0x to support C++11 functionality
 * without warnings (functionality used by the macros below).  These modes are
 * detectable by checking whether __GXX_EXPERIMENTAL_CXX0X__ is defined or, more
 * standardly, by checking whether __cplusplus has a C++11 or greater value.
 * Current versions of g++ do not correctly set __cplusplus, so we check both
 * for forward compatibility.
 *
 * Even though some versions of MSVC support explicit conversion operators, we
 * don't indicate support for them here, due to
 * http://stackoverflow.com/questions/20498142/visual-studio-2013-explicit-keyword-bug
 */
#  if _MSC_VER >= 1800
#    define MOZ_HAVE_CXX11_DELETE
#  endif
# if _MSC_VER >= 1800
#define MOZ_HAVE_CXX11_DEFAULT
#endif
#  if _MSC_VER >= 1700
#    define MOZ_HAVE_CXX11_FINAL         final
#  else
#    if defined(__clang__)
#      error Please do not try to use clang-cl with MSVC10 or below emulation!
#    endif
     /* MSVC <= 10 used to spell "final" as "sealed". */
#    define MOZ_HAVE_CXX11_FINAL         sealed
#  endif
#  define MOZ_HAVE_CXX11_OVERRIDE
#  define MOZ_HAVE_NEVER_INLINE          __declspec(noinline)
#  define MOZ_HAVE_NORETURN              __declspec(noreturn)
#  if _MSC_VER >= 1900
#    define MOZ_HAVE_CXX11_CONSTEXPR
#    define MOZ_HAVE_CXX11_CONSTEXPR_IN_TEMPLATES
#    define MOZ_HAVE_EXPLICIT_CONVERSION
#  endif
#  ifdef __clang__
     /* clang-cl probably supports constexpr and explicit conversions. */
#    if __has_extension(cxx_constexpr)
#      define MOZ_HAVE_CXX11_CONSTEXPR
#    endif
#    if __has_extension(cxx_explicit_conversions)
#      define MOZ_HAVE_EXPLICIT_CONVERSION
#    endif
#  endif
#elif defined(__clang__)
   /*
    * Per Clang documentation, "Note that marketing version numbers should not
    * be used to check for language features, as different vendors use different
    * numbering schemes. Instead, use the feature checking macros."
    */
#  ifndef __has_extension
#    define __has_extension __has_feature /* compatibility, for older versions of clang */
#  endif
#  if __has_extension(cxx_constexpr)
#    define MOZ_HAVE_CXX11_CONSTEXPR
#  endif
#  if __has_extension(cxx_explicit_conversions)
#    define MOZ_HAVE_EXPLICIT_CONVERSION
#  endif
#  if __has_extension(cxx_override_control)
#    define MOZ_HAVE_CXX11_OVERRIDE
#    define MOZ_HAVE_CXX11_FINAL         final
#  endif
#  if __has_attribute(noinline)
#    define MOZ_HAVE_NEVER_INLINE        __attribute__((noinline))
#  endif
#  if __has_attribute(noreturn)
#    define MOZ_HAVE_NORETURN            __attribute__((noreturn))
#  endif
#elif defined(__GNUC__)
#  if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L
#    if MOZ_GCC_VERSION_AT_LEAST(4, 7, 0)
#      define MOZ_HAVE_CXX11_OVERRIDE
#      define MOZ_HAVE_CXX11_FINAL       final
#    endif
#    if MOZ_GCC_VERSION_AT_LEAST(4, 6, 0)
#      define MOZ_HAVE_CXX11_CONSTEXPR
#    endif
#      define MOZ_HAVE_EXPLICIT_CONVERSION
#  else
     /* __final is a non-C++11 GCC synonym for 'final', per GCC r176655. */
#    if MOZ_GCC_VERSION_AT_LEAST(4, 7, 0)
#      define MOZ_HAVE_CXX11_FINAL       __final
#    endif
#  endif
#  define MOZ_HAVE_NEVER_INLINE          __attribute__((noinline))
#  define MOZ_HAVE_NORETURN              __attribute__((noreturn))
#endif

/*
 * When built with clang analyzer (a.k.a scan-build), define MOZ_HAVE_NORETURN
 * to mark some false positives
 */
#ifdef __clang_analyzer__
#  if __has_extension(attribute_analyzer_noreturn)
#    define MOZ_HAVE_ANALYZER_NORETURN __attribute__((analyzer_noreturn))
#  endif
#endif

/*
 * The MOZ_CONSTEXPR specifier declares that a C++11 compiler can evaluate a
 * function at compile time. A constexpr function cannot examine any values
 * except its arguments and can have no side effects except its return value.
 * The MOZ_CONSTEXPR_VAR specifier tells a C++11 compiler that a variable's
 * value may be computed at compile time.  It should be prefered to just
 * marking variables as MOZ_CONSTEXPR because if the compiler does not support
 * constexpr it will fall back to making the variable const, and some compilers
 * do not accept variables being marked both const and constexpr.
 */
#ifdef MOZ_HAVE_CXX11_CONSTEXPR
#  define MOZ_CONSTEXPR         constexpr
#  define MOZ_CONSTEXPR_VAR     constexpr
#else
#  define MOZ_CONSTEXPR         /* no support */
#  define MOZ_CONSTEXPR_VAR     const
#endif

/*
 * MOZ_EXPLICIT_CONVERSION is a specifier on a type conversion
 * overloaded operator that declares that a C++11 compiler should restrict
 * this operator to allow only explicit type conversions, disallowing
 * implicit conversions.
 *
 * Example:
 *
 *   template<typename T>
 *   class Ptr
 *   {
 *     T* mPtr;
 *     MOZ_EXPLICIT_CONVERSION operator bool() const
 *     {
 *       return mPtr != nullptr;
 *     }
 *   };
 *
 */
#ifdef MOZ_HAVE_EXPLICIT_CONVERSION
#  define MOZ_EXPLICIT_CONVERSION explicit
#else
#  define MOZ_EXPLICIT_CONVERSION /* no support */
#endif

/*
 * MOZ_NEVER_INLINE is a macro which expands to tell the compiler that the
 * method decorated with it must never be inlined, even if the compiler would
 * otherwise choose to inline the method.  Compilers aren't absolutely
 * guaranteed to support this, but most do.
 */
#if defined(MOZ_HAVE_NEVER_INLINE)
#  define MOZ_NEVER_INLINE      MOZ_HAVE_NEVER_INLINE
#else
#  define MOZ_NEVER_INLINE      /* no support */
#endif

/*
 * MOZ_NORETURN, specified at the start of a function declaration, indicates
 * that the given function does not return.  (The function definition does not
 * need to be annotated.)
 *
 *   MOZ_NORETURN void abort(const char* msg);
 *
 * This modifier permits the compiler to optimize code assuming a call to such a
 * function will never return.  It also enables the compiler to avoid spurious
 * warnings about not initializing variables, or about any other seemingly-dodgy
 * operations performed after the function returns.
 *
 * This modifier does not affect the corresponding function's linking behavior.
 */
#if defined(MOZ_HAVE_NORETURN)
#  define MOZ_NORETURN          MOZ_HAVE_NORETURN
#else
#  define MOZ_NORETURN          /* no support */
#endif

/**
 * MOZ_COLD tells the compiler that a function is "cold", meaning infrequently
 * executed. This may lead it to optimize for size more aggressively than speed,
 * or to allocate the body of the function in a distant part of the text segment
 * to help keep it from taking up unnecessary icache when it isn't in use.
 *
 * Place this attribute at the very beginning of a function definition. For
 * example, write
 *
 *   MOZ_COLD int foo();
 *
 * or
 *
 *   MOZ_COLD int foo() { return 42; }
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MOZ_COLD __attribute__ ((cold))
#else
#  define MOZ_COLD
#endif

/**
 * MOZ_NONNULL tells the compiler that some of the arguments to a function are
 * known to be non-null. The arguments are a list of 1-based argument indexes
 * identifying arguments which are known to be non-null.
 *
 * Place this attribute at the very beginning of a function definition. For
 * example, write
 *
 *   MOZ_NONNULL(1, 2) int foo(char *p, char *q);
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MOZ_NONNULL(...) __attribute__ ((nonnull(__VA_ARGS__)))
#else
#  define MOZ_NONNULL(...)
#endif

/*
 * MOZ_PRETEND_NORETURN_FOR_STATIC_ANALYSIS, specified at the end of a function
 * declaration, indicates that for the purposes of static analysis, this
 * function does not return.  (The function definition does not need to be
 * annotated.)
 *
 * MOZ_ReportCrash(const char* s, const char* file, int ln)
 *   MOZ_PRETEND_NORETURN_FOR_STATIC_ANALYSIS
 *
 * Some static analyzers, like scan-build from clang, can use this information
 * to eliminate false positives.  From the upstream documentation of scan-build:
 * "This attribute is useful for annotating assertion handlers that actually
 * can return, but for the purpose of using the analyzer we want to pretend
 * that such functions do not return."
 *
 */
#if defined(MOZ_HAVE_ANALYZER_NORETURN)
#  define MOZ_PRETEND_NORETURN_FOR_STATIC_ANALYSIS          MOZ_HAVE_ANALYZER_NORETURN
#else
#  define MOZ_PRETEND_NORETURN_FOR_STATIC_ANALYSIS          /* no support */
#endif

/*
 * MOZ_ASAN_BLACKLIST is a macro to tell AddressSanitizer (a compile-time
 * instrumentation shipped with Clang and GCC) to not instrument the annotated
 * function. Furthermore, it will prevent the compiler from inlining the
 * function because inlining currently breaks the blacklisting mechanism of
 * AddressSanitizer.
 */
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define MOZ_HAVE_ASAN_BLACKLIST
#  endif
#elif defined(__GNUC__)
#  if defined(__SANITIZE_ADDRESS__)
#    define MOZ_HAVE_ASAN_BLACKLIST
#  endif
#endif

#if defined(MOZ_HAVE_ASAN_BLACKLIST)
#  define MOZ_ASAN_BLACKLIST MOZ_NEVER_INLINE __attribute__((no_sanitize_address))
#else
#  define MOZ_ASAN_BLACKLIST /* nothing */
#endif

/*
 * MOZ_TSAN_BLACKLIST is a macro to tell ThreadSanitizer (a compile-time
 * instrumentation shipped with Clang) to not instrument the annotated function.
 * Furthermore, it will prevent the compiler from inlining the function because
 * inlining currently breaks the blacklisting mechanism of ThreadSanitizer.
 */
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define MOZ_TSAN_BLACKLIST MOZ_NEVER_INLINE __attribute__((no_sanitize_thread))
#  else
#    define MOZ_TSAN_BLACKLIST /* nothing */
#  endif
#else
#  define MOZ_TSAN_BLACKLIST /* nothing */
#endif

/**
 * MOZ_MUST_USE tells the compiler to emit a warning if a function's
 * return value is not used by the caller.
 *
 * Place this attribute at the very beginning of a function declaration. For
 * example, write
 *
 *   MOZ_MUST_USE int foo();
 *
 * or
 *
 *   MOZ_MUST_USE int foo() { return 42; }
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MOZ_MUST_USE __attribute__ ((warn_unused_result))
#else
#  define MOZ_MUST_USE
#endif


#ifdef __cplusplus

/*
 * MOZ_DELETE, specified immediately prior to the ';' terminating an undefined-
 * method declaration, attempts to delete that method from the corresponding
 * class.  An attempt to use the method will always produce an error *at compile
 * time* (instead of sometimes as late as link time) when this macro can be
 * implemented.  For example, you can use MOZ_DELETE to produce classes with no
 * implicit copy constructor or assignment operator:
 *
 *   struct NonCopyable
 *   {
 *   private:
 *     NonCopyable(const NonCopyable& aOther) MOZ_DELETE;
 *     void operator=(const NonCopyable& aOther) MOZ_DELETE;
 *   };
 *
 * If MOZ_DELETE can't be implemented for the current compiler, use of the
 * annotated method will still cause an error, but the error might occur at link
 * time in some cases rather than at compile time.
 *
 * MOZ_DELETE relies on C++11 functionality not universally implemented.  As a
 * backstop, method declarations using MOZ_DELETE should be private.
 */
#if defined(MOZ_HAVE_CXX11_DELETE)
#  define MOZ_DELETE            = delete
#else
#  define MOZ_DELETE            /* no support */
#endif

#if defined(MOZ_HAVE_CXX11_DEFAULT)
#define MOZ_DEFAULT				= default
#else
#define MOZ_DEFAULT				/* no support */
#endif

/**
 * MOZ_WARN_UNUSED_RESULT tells the compiler to emit a warning if a function's
 * return value is not used by the caller.
 *
 * Place this attribute at the very beginning of a function definition. For
 * example, write
 *
 *   MOZ_WARN_UNUSED_RESULT int foo();
 *
 * or
 *
 *   MOZ_WARN_UNUSED_RESULT int foo() { return 42; }
 */
#if defined(__GNUC__) || defined(__clang__)
#  define MOZ_WARN_UNUSED_RESULT __attribute__ ((warn_unused_result))
#else
#  define MOZ_WARN_UNUSED_RESULT
#endif

/*
 * MOZ_OVERRIDE explicitly indicates that a virtual member function in a class
 * overrides a member function of a base class, rather than potentially being a
 * new member function.  MOZ_OVERRIDE should be placed immediately before the
 * ';' terminating the member function's declaration, or before '= 0;' if the
 * member function is pure.  If the member function is defined in the class
 * definition, it should appear before the opening brace of the function body.
 *
 *   class Base
 *   {
 *   public:
 *     virtual void f() = 0;
 *   };
 *   class Derived1 : public Base
 *   {
 *   public:
 *     virtual void f() MOZ_OVERRIDE;
 *   };
 *   class Derived2 : public Base
 *   {
 *   public:
 *     virtual void f() MOZ_OVERRIDE = 0;
 *   };
 *   class Derived3 : public Base
 *   {
 *   public:
 *     virtual void f() MOZ_OVERRIDE { }
 *   };
 *
 * In compilers supporting C++11 override controls, MOZ_OVERRIDE *requires* that
 * the function marked with it override a member function of a base class: it
 * is a compile error if it does not.  Otherwise MOZ_OVERRIDE does not affect
 * semantics and merely documents the override relationship to the reader (but
 * of course must still be used correctly to not break C++11 compilers).
 */
#if defined(__clang__) && __clang_major__ >= 3
# define MOZ_HAVE_CXX11_OVERRIDE
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
# if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L
#  define MOZ_HAVE_CXX11_OVERRIDE
# endif
#endif
#if defined(MOZ_HAVE_CXX11_OVERRIDE)
# define MOZ_OVERRIDE          override
#else
# define MOZ_OVERRIDE          /* no override support, or unknown support */
#endif

/*
 * MOZ_FINAL indicates that some functionality cannot be overridden through
 * inheritance.  It can be used to annotate either classes/structs or virtual
 * member functions.
 *
 * To annotate a class/struct with MOZ_FINAL, place MOZ_FINAL immediately after
 * the name of the class, before the list of classes from which it derives (if
 * any) and before its opening brace.  MOZ_FINAL must not be used to annotate
 * unnamed classes or structs.  (With some compilers, and with C++11 proper, the
 * underlying expansion is ambiguous with specifying a class name.)
 *
 *   class Base MOZ_FINAL
 *   {
 *   public:
 *     Base();
 *     ~Base();
 *     virtual void f() { }
 *   };
 *   // This will be an error in some compilers:
 *   class Derived : public Base
 *   {
 *   public:
 *     ~Derived() { }
 *   };
 *
 * One particularly common reason to specify MOZ_FINAL upon a class is to tell
 * the compiler that it's not dangerous for it to have a non-virtual destructor
 * yet have one or more virtual functions, silencing the warning it might emit
 * in this case.  Suppose Base above weren't annotated with MOZ_FINAL.  Because
 * ~Base() is non-virtual, an attempt to delete a Derived* through a Base*
 * wouldn't call ~Derived(), so any cleanup ~Derived() might do wouldn't happen.
 * (Formally C++ says behavior is undefined, but compilers will likely just call
 * ~Base() and not ~Derived().)  Specifying MOZ_FINAL tells the compiler that
 * it's safe for the destructor to be non-virtual.
 *
 * In compilers implementing final controls, it is an error to inherit from a
 * class annotated with MOZ_FINAL.  In other compilers it serves only as
 * documentation.
 *
 * To annotate a virtual member function with MOZ_FINAL, place MOZ_FINAL
 * immediately before the ';' terminating the member function's declaration, or
 * before '= 0;' if the member function is pure.  If the member function is
 * defined in the class definition, it should appear before the opening brace of
 * the function body.  (This placement is identical to that for MOZ_OVERRIDE.
 * If both are used, they should appear in the order 'MOZ_FINAL MOZ_OVERRIDE'
 * for consistency.)
 *
 *   class Base
 *   {
 *   public:
 *     virtual void f() MOZ_FINAL;
 *   };
 *   class Derived
 *   {
 *   public:
 *     // This will be an error in some compilers:
 *     virtual void f();
 *   };
 *
 * In compilers implementing final controls, it is an error for a derived class
 * to override a method annotated with MOZ_FINAL.  In other compilers it serves
 * only as documentation.
 */
#if defined(MOZ_HAVE_CXX11_FINAL)
#  define MOZ_FINAL             MOZ_HAVE_CXX11_FINAL
#else
#  define MOZ_FINAL             /* no support */
#endif

#if defined (_MSC_VER) && _MSC_VER <= 1700
#define final MOZ_FINAL
#endif

/*
 * The following macros are attributes that support the static analysis plugin
 * included with Mozilla, and will be implemented (when such support is enabled)
 * as C++11 attributes. Since such attributes are legal pretty much everywhere
 * and have subtly different semantics depending on their placement, the
 * following is a guide on where to place the attributes.
 *
 * Attributes that apply to a struct or class precede the name of the class:
 * (Note that this is different from the placement of final for classes!)
 *
 *   class MOZ_CLASS_ATTRIBUTE SomeClass {};
 *
 * Attributes that apply to functions follow the parentheses and const
 * qualifiers but precede final, override and the function body:
 *
 *   void DeclaredFunction() MOZ_FUNCTION_ATTRIBUTE;
 *   void SomeFunction() MOZ_FUNCTION_ATTRIBUTE {}
 *   void PureFunction() const MOZ_FUNCTION_ATTRIBUTE = 0;
 *   void OverriddenFunction() MOZ_FUNCTION_ATTIRBUTE override;
 *
 * Attributes that apply to variables or parameters follow the variable's name:
 *
 *   int variable MOZ_VARIABLE_ATTRIBUTE;
 *
 * Attributes that apply to types follow the type name:
 *
 *   typedef int MOZ_TYPE_ATTRIBUTE MagicInt;
 *   int MOZ_TYPE_ATTRIBUTE someVariable;
 *   int* MOZ_TYPE_ATTRIBUTE magicPtrInt;
 *   int MOZ_TYPE_ATTRIBUTE* ptrToMagicInt;
 *
 * Attributes that apply to statements precede the statement:
 *
 *   MOZ_IF_ATTRIBUTE if (x == 0)
 *   MOZ_DO_ATTRIBUTE do { } while (0);
 *
 * Attributes that apply to labels precede the label:
 *
 *   MOZ_LABEL_ATTRIBUTE target:
 *     goto target;
 *   MOZ_CASE_ATTRIBUTE case 5:
 *   MOZ_DEFAULT_ATTRIBUTE default:
 *
 * The static analyses that are performed by the plugin are as follows:
 *
 * MOZ_MUST_OVERRIDE: Applies to all C++ member functions. All immediate
 *   subclasses must provide an exact override of this method; if a subclass
 *   does not override this method, the compiler will emit an error. This
 *   attribute is not limited to virtual methods, so if it is applied to a
 *   nonvirtual method and the subclass does not provide an equivalent
 *   definition, the compiler will emit an error.
 * MOZ_STACK_CLASS: Applies to all classes. Any class with this annotation is
 *   expected to live on the stack, so it is a compile-time error to use it, or
 *   an array of such objects, as a global or static variable, or as the type of
 *   a new expression (unless placement new is being used). If a member of
 *   another class uses this class, or if another class inherits from this
 *   class, then it is considered to be a stack class as well, although this
 *   attribute need not be provided in such cases.
 * MOZ_NONHEAP_CLASS: Applies to all classes. Any class with this annotation is
 *   expected to live on the stack or in static storage, so it is a compile-time
 *   error to use it, or an array of such objects, as the type of a new
 *   expression. If a member of another class uses this class, or if another
 *   class inherits from this class, then it is considered to be a non-heap class
 *   as well, although this attribute need not be provided in such cases.
 * MOZ_HEAP_CLASS: Applies to all classes. Any class with this annotation is
 *   expected to live on the heap, so it is a compile-time error to use it, or
 *   an array of such objects, as the type of a variable declaration, or as a
 *   temporary object. If a member of another class uses this class, or if
 *   another class inherits from this class, then it is considered to be a heap
 *   class as well, although this attribute need not be provided in such cases.
 * MOZ_NON_TEMPORARY_CLASS: Applies to all classes. Any class with this
 *   annotation is expected not to live in a temporary. If a member of another
 *   class uses this class or if another class inherits from this class, then it
 *   is considered to be a non-temporary class as well, although this attribute
 *   need not be provided in such cases.
 * MOZ_RAII: Applies to all classes. Any class with this annotation is assumed
 *   to be a RAII guard, which is expected to live on the stack in an automatic
 *   allocation. It is prohibited from being allocated in a temporary, static
 *   storage, or on the heap. This is a combination of MOZ_STACK_CLASS and
 *   MOZ_NON_TEMPORARY_CLASS.
 * MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS: Applies to all classes that are
 *   intended to prevent introducing static initializers.  This attribute
 *   currently makes it a compile-time error to instantiate these classes
 *   anywhere other than at the global scope, or as a static member of a class.
 * MOZ_TRIVIAL_CTOR_DTOR: Applies to all classes that must have both a trivial
 *   constructor and a trivial destructor.  Setting this attribute on a class
 *   makes it a compile-time error for that class to get a non-trivial
 *   constructor or destructor for any reason.
 * MOZ_HEAP_ALLOCATOR: Applies to any function. This indicates that the return
 *   value is allocated on the heap, and will as a result check such allocations
 *   during MOZ_STACK_CLASS and MOZ_NONHEAP_CLASS annotation checking.
 * MOZ_IMPLICIT: Applies to constructors. Implicit conversion constructors
 *   are disallowed by default unless they are marked as MOZ_IMPLICIT. This
 *   attribute must be used for constructors which intend to provide implicit
 *   conversions.
 * MOZ_NO_ARITHMETIC_EXPR_IN_ARGUMENT: Applies to functions. Makes it a compile
 *   time error to pass arithmetic expressions on variables to the function.
 * MOZ_OWNING_REF: Applies to declarations of pointers to reference counted
 *   types.  This attribute tells the compiler that the raw pointer is a strong
 *   reference, where ownership through methods such as AddRef and Release is
 *   managed manually.  This can make the compiler ignore these pointers when
 *   validating the usage of pointers otherwise.
 *
 *   Example uses include owned pointers inside of unions, and pointers stored
 *   in POD types where a using a smart pointer class would make the object
 *   non-POD.
 * MOZ_NON_OWNING_REF: Applies to declarations of pointers to reference counted
 *   types.  This attribute tells the compiler that the raw pointer is a weak
 *   reference, which is ensured to be valid by a guarantee that the reference
 *   will be nulled before the pointer becomes invalid.  This can make the compiler
 *   ignore these pointers when validating the usage of pointers otherwise.
 *
 *   Examples include an mOwner pointer, which is nulled by the owning class's
 *   destructor, and is null-checked before dereferencing.
 * MOZ_UNSAFE_REF: Applies to declarations of pointers to reference counted types.
 *   Occasionally there are non-owning references which are valid, but do not take
 *   the form of a MOZ_NON_OWNING_REF.  Their safety may be dependent on the behaviour
 *   of API consumers.  The string argument passed to this macro documents the safety
 *   conditions.  This can make the compiler ignore these pointers when validating
 *   the usage of pointers elsewhere.
 *
 *   Examples include an nsIAtom* member which is known at compile time to point to a
 *   static atom which is valid throughout the lifetime of the program, or an API which
 *   stores a pointer, but doesn't take ownership over it, instead requiring the API
 *   consumer to correctly null the value before it becomes invalid.
 *
 *   Use of this annotation is discouraged when a strong reference or one of the above
 *   two annotations can be used instead.
 * MOZ_NO_ADDREF_RELEASE_ON_RETURN: Applies to function declarations.  Makes it
 *   a compile time error to call AddRef or Release on the return value of a
 *   function.  This is intended to be used with operator->() of our smart
 *   pointer classes to ensure that the refcount of an object wrapped in a
 *   smart pointer is not manipulated directly.
 * MOZ_MUST_USE: Applies to type declarations.  Makes it a compile time error to not
 *   use the return value of a function which has this type.  This is intended to be
 *   used with types which it is an error to not use.
 * MOZ_NEEDS_NO_VTABLE_TYPE: Applies to template class declarations.  Makes it
 *   a compile time error to instantiate this template with a type parameter which
 *   has a VTable.
 * MOZ_NON_MEMMOVABLE: Applies to class declarations for types that are not safe
 *   to be moved in memory using memmove().
 * MOZ_NEEDS_MEMMOVABLE_TYPE: Applies to template class declarations where the
 *   template arguments are required to be safe to move in memory using
 *   memmove().  Passing MOZ_NON_MEMMOVABLE types to these templates is a
 *   compile time error.
 * MOZ_INHERIT_TYPE_ANNOTATIONS_FROM_TEMPLATE_ARGS: Applies to template class
 *   declarations where an instance of the template should be considered, for
 *   static analysis purposes, to inherit any type annotations (such as
 *   MOZ_MUST_USE and MOZ_STACK_CLASS) from its template arguments.
 * MOZ_NON_AUTOABLE: Applies to class declarations. Makes it a compile time error to
 *   use `auto` in place of this type in variable declarations.  This is intended to
 *   be used with types which are intended to be implicitly constructed into other
 *   other types before being assigned to variables.
 */
#ifdef MOZ_CLANG_PLUGIN
#  define MOZ_MUST_OVERRIDE __attribute__((annotate("moz_must_override")))
#  define MOZ_STACK_CLASS __attribute__((annotate("moz_stack_class")))
#  define MOZ_NONHEAP_CLASS __attribute__((annotate("moz_nonheap_class")))
#  define MOZ_HEAP_CLASS __attribute__((annotate("moz_heap_class")))
#  define MOZ_NON_TEMPORARY_CLASS __attribute__((annotate("moz_non_temporary_class")))
#  define MOZ_TRIVIAL_CTOR_DTOR __attribute__((annotate("moz_trivial_ctor_dtor")))
#  ifdef DEBUG
     /* in debug builds, these classes do have non-trivial constructors. */
#    define MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS __attribute__((annotate("moz_global_class")))
#  else
#    define MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS __attribute__((annotate("moz_global_class"))) \
            MOZ_TRIVIAL_CTOR_DTOR
#  endif
#  define MOZ_IMPLICIT __attribute__((annotate("moz_implicit")))
#  define MOZ_NO_ARITHMETIC_EXPR_IN_ARGUMENT __attribute__((annotate("moz_no_arith_expr_in_arg")))
#  define MOZ_OWNING_REF __attribute__((annotate("moz_strong_ref")))
#  define MOZ_NON_OWNING_REF __attribute__((annotate("moz_weak_ref")))
#  define MOZ_UNSAFE_REF(reason) __attribute__((annotate("moz_weak_ref")))
#  define MOZ_NO_ADDREF_RELEASE_ON_RETURN __attribute__((annotate("moz_no_addref_release_on_return")))
#  define MOZ_MUST_USE __attribute__((annotate("moz_must_use")))
#  define MOZ_NEEDS_NO_VTABLE_TYPE __attribute__((annotate("moz_needs_no_vtable_type")))
#  define MOZ_NON_MEMMOVABLE __attribute__((annotate("moz_non_memmovable")))
#  define MOZ_NEEDS_MEMMOVABLE_TYPE __attribute__((annotate("moz_needs_memmovable_type")))
#  define MOZ_INHERIT_TYPE_ANNOTATIONS_FROM_TEMPLATE_ARGS \
    __attribute__((annotate("moz_inherit_type_annotations_from_template_args")))
#  define MOZ_NON_AUTOABLE __attribute__((annotate("moz_non_autoable")))
/*
 * It turns out that clang doesn't like void func() __attribute__ {} without a
 * warning, so use pragmas to disable the warning. This code won't work on GCC
 * anyways, so the warning is safe to ignore.
 */
#  define MOZ_HEAP_ALLOCATOR \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wgcc-compat\"") \
    __attribute__((annotate("moz_heap_allocator"))) \
    _Pragma("clang diagnostic pop")
#else
#  define MOZ_MUST_OVERRIDE /* nothing */
#  define MOZ_STACK_CLASS /* nothing */
#  define MOZ_NONHEAP_CLASS /* nothing */
#  define MOZ_HEAP_CLASS /* nothing */
#  define MOZ_NON_TEMPORARY_CLASS /* nothing */
#  define MOZ_TRIVIAL_CTOR_DTOR /* nothing */
#  define MOZ_ONLY_USED_TO_AVOID_STATIC_CONSTRUCTORS /* nothing */
#  define MOZ_IMPLICIT /* nothing */
#  define MOZ_NO_ARITHMETIC_EXPR_IN_ARGUMENT /* nothing */
#  define MOZ_HEAP_ALLOCATOR /* nothing */
#  define MOZ_OWNING_REF /* nothing */
#  define MOZ_NON_OWNING_REF /* nothing */
#  define MOZ_UNSAFE_REF(reason) /* nothing */
#  define MOZ_NO_ADDREF_RELEASE_ON_RETURN /* nothing */
#  define MOZ_MUST_USE /* nothing */
#  define MOZ_NEEDS_NO_VTABLE_TYPE /* nothing */
#  define MOZ_NON_MEMMOVABLE /* nothing */
#  define MOZ_NEEDS_MEMMOVABLE_TYPE /* nothing */
#  define MOZ_INHERIT_TYPE_ANNOTATIONS_FROM_TEMPLATE_ARGS /* nothing */
#  define MOZ_NON_AUTOABLE /* nothing */
#endif /* MOZ_CLANG_PLUGIN */

/*
 * MOZ_THIS_IN_INITIALIZER_LIST is used to avoid a warning when we know that
 * it's safe to use 'this' in an initializer list.
 */
#ifdef _MSC_VER
#  define MOZ_THIS_IN_INITIALIZER_LIST() \
     __pragma(warning(push)) \
     __pragma(warning(disable:4355)) \
     this \
     __pragma(warning(pop))
#else
#  define MOZ_THIS_IN_INITIALIZER_LIST() this
#endif

#endif /* __cplusplus */

#endif /* mozilla_Attributes_h */
