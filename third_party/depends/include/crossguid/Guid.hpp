/*
The MIT License (MIT)

Copyright (c) 2014 Graeme Hill (http://graemehill.ca)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#ifdef GUID_ANDROID
#include <jni.h>
#endif

#include <functional>
#include <iostream>
#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <iomanip>

#define BEGIN_XG_NAMESPACE namespace xg {
#define END_XG_NAMESPACE }

BEGIN_XG_NAMESPACE

// Class to represent a GUID/UUID. Each instance acts as a wrapper around a
// 16 byte value that can be passed around by value. It also supports
// conversion to string (via the stream operator <<) and conversion from a
// string via constructor.
class Guid
{
public:
	Guid(const std::array<unsigned char, 16> &bytes);
	Guid(const unsigned char *bytes);
	Guid(const std::string &fromString);
	Guid();
	Guid(const Guid &other);
	Guid &operator=(const Guid &other);
	bool operator==(const Guid &other) const;
	bool operator!=(const Guid &other) const;

	std::string str() const;
	operator std::string() const;
	void swap(Guid &other);
	bool isValid() const;

private:
	void zeroify();

	// actual data
	std::array<unsigned char, 16> _bytes;

	// make the << operator a friend so it can access _bytes
	friend std::ostream &operator<<(std::ostream &s, const Guid &guid);
};

Guid newGuid();

#ifdef GUID_ANDROID
struct AndroidGuidInfo
{
	static AndroidGuidInfo fromJniEnv(JNIEnv *env);

	JNIEnv *env;
	jclass uuidClass;
	jmethodID newGuidMethod;
	jmethodID mostSignificantBitsMethod;
	jmethodID leastSignificantBitsMethod;
};

extern AndroidGuidInfo androidInfo;

void initJni(JNIEnv *env);
#endif

END_XG_NAMESPACE

namespace std
{
	// Template specialization for std::swap<Guid>() --
	// See guid.cpp for the function definition
	template <>
	void swap(xg::Guid &guid0, xg::Guid &guid1);

	// Specialization for std::hash<Guid> -- this implementation
	// uses std::hash<std::string> on the stringification of the guid
	// to calculate the hash
	template <>
	struct hash<xg::Guid>
	{
		typedef xg::Guid argument_type;
		typedef std::size_t result_type;

		result_type operator()(argument_type const &guid) const
		{
			std::hash<std::string> hasher;
			return static_cast<result_type>(hasher(guid.str()));
		}
	};
}
