/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Yul interpreter.
 */

#include <exception>
#include <test/tools/yulInterpreter/Inspector.h>
#include <test/tools/yulInterpreter/Interpreter.h>

#include <libyul/AsmAnalysis.h>
#include <libyul/AsmAnalysisInfo.h>
#include <libyul/Dialect.h>
#include <libyul/YulStack.h>
#include <libyul/backends/evm/EVMDialect.h>

#include <liblangutil/DebugInfoSelection.h>
#include <liblangutil/EVMVersion.h>
#include <liblangutil/Exceptions.h>
#include <liblangutil/SourceReferenceFormatter.h>

#include <libsolutil/CommonData.h>
#include <libsolutil/CommonIO.h>
#include <libsolutil/Exceptions.h>

#include <boost/program_options.hpp>

#include <iostream>
#include <memory>
#include <string>

using namespace std;
using namespace solidity;
using namespace solidity::util;
using namespace solidity::langutil;
using namespace solidity::yul;
using namespace solidity::yul::test;

namespace po = boost::program_options;

namespace
{

pair<shared_ptr<Block>, shared_ptr<AsmAnalysisInfo>> parse(string const& _source)
{
	YulStack stack(
		langutil::EVMVersion(),
		nullopt,
		YulStack::Language::StrictAssembly,
		solidity::frontend::OptimiserSettings::none(),
		DebugInfoSelection::Default());
	if (stack.parseAndAnalyze("--INPUT--", _source))
	{
		yulAssert(stack.errors().empty(), "Parsed successfully but had errors.");
		return make_pair(stack.parserResult()->code, stack.parserResult()->analysisInfo);
	}
	else
	{
		SourceReferenceFormatter(cout, stack, true, false).printErrorInformation(stack.errors());
		return {};
	}
}

void interpret(string const& _source, bool _inspect, bool _disableExternalCalls, bytes calldata = bytes())
{
	shared_ptr<Block> ast;
	shared_ptr<AsmAnalysisInfo> analysisInfo;
	tie(ast, analysisInfo) = parse(_source);
	if (!ast || !analysisInfo)
		return;

	InterpreterState state;
	state.call_context.calldata = calldata;
	state.maxTraceSize = 10000;
	state.call_context.callvalue = 0;
	try
	{
		Dialect const& dialect(EVMDialect::strictAssemblyForEVMObjects(langutil::EVMVersion{}));

		if (_inspect)
			InspectedInterpreter::
				run(std::make_shared<Inspector>(_source, state),
					state,
					dialect,
					*ast,
					_disableExternalCalls,
					/*disableMemoryTracing=*/false);

		else
			Interpreter::run(state, dialect, *ast, _disableExternalCalls, /*disableMemoryTracing=*/false);
	}
	catch (InterpreterTerminatedGeneric const&)
	{
	}

	state.dumpTraceAndState(cout, /*disableMemoryTracing=*/false);
}

}

int main(int argc, char** argv)
{
	po::options_description options(
		R"(yulrun, the Yul interpreter.
Usage: yulrun [Options] < input
Reads a single source from stdin, runs it and prints a trace of all side-effects.

Allowed options)",
		po::options_description::m_default_line_length,
		po::options_description::m_default_line_length - 23);
	options.add_options()("help", "Show this help screen.")("enable-external-calls", "Enable external calls")(
		"interactive", "Run interactive")("input-file", po::value<vector<string>>(), "input file")(
		"calldata", po::value<string>(), "Calldata to be passed to the contract function")(
		"callvalue", po::value<string>(), "Callvalue to be passed to the transaction");
	po::positional_options_description filesPositions;
	filesPositions.add("input-file", -1);

	po::variables_map arguments;
	try
	{
		po::command_line_parser cmdLineParser(argc, argv);
		cmdLineParser.options(options).positional(filesPositions);
		po::store(cmdLineParser.run(), arguments);
	}
	catch (po::error const& _exception)
	{
		cerr << _exception.what() << endl;
		return 1;
	}

	if (arguments.count("help"))
		cout << options;
	else
	{
		string input;
		if (arguments.count("input-file"))
			for (string path: arguments["input-file"].as<vector<string>>())
			{
				try
				{
					input += readFileAsString(path);
				}
				catch (FileNotFound const&)
				{
					cerr << "File not found: " << path << endl;
					return 1;
				}
				catch (NotAFile const&)
				{
					cerr << "Not a regular file: " << path << endl;
					return 1;
				}
			}
		else
			input = readUntilEnd(cin);

		bytes calldata_bytes;
		if (arguments.count("calldata"))
		{
			string calldata = arguments["calldata"].as<string>();
			try
			{
				calldata_bytes = fromHex(calldata);
			}
			catch (std::exception const&)
			{
				cerr << "Invalid calldata: " << calldata << endl;
				return 1;
			}
		}

		interpret(input, arguments.count("interactive"), !arguments.count("enable-external-calls"), calldata_bytes);
	}

	return 0;
}
