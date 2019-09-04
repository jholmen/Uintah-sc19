/*
 * The MIT License
 *
 * Copyright (c) 1997-2018 The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include <Core/Exceptions/ProblemSetupException.h>
#include <iostream>
#include <sstream>

using namespace Uintah;

ProblemSetupException::ProblemSetupException(const std::string& msg, const char* file, int line, bool ignoreWait)
    : Exception(ignoreWait), d_msg(msg) 
{
  std::ostringstream s;
  s << "\n"
    << "!! WARNING: Your .ups file did not parse successfully...\n"
    << "!!          Fix your .ups file or update the ups_spec.xml\n"
    << "!!          specification.  Reason for failure is:\n"
    << "\n"
    << "ProblemSetupException thrown: " << file << ", line: " << line << "\n"
    << d_msg
    << "\n";
  d_msg = s.str();

#ifdef EXCEPTIONS_CRASH
  std::cout << d_msg << "\n";
#endif
}

ProblemSetupException::ProblemSetupException(const ProblemSetupException& copy)
    : d_msg(copy.d_msg)
{
}

ProblemSetupException::~ProblemSetupException()
{
}

const char* ProblemSetupException::message() const
{
    return d_msg.c_str();
}

const char* ProblemSetupException::type() const
{
    return "Uintah::Exceptions::ProblemSetupException";
}
