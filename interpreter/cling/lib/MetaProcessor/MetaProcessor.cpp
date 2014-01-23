//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/MetaProcessor/MetaProcessor.h"

#include "Display.h"
#include "InputValidator.h"
#include "MetaParser.h"
#include "MetaSema.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/StoredValueRef.h"

#include "clang/Basic/FileManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"

#include "llvm/Support/Path.h"

#include <fstream>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <stdio.h>


using namespace clang;

namespace cling {

  MetaProcessor::MaybeRedirectOutputRAII::MaybeRedirectOutputRAII(
                                          MetaProcessor* p)
  :m_MetaProcessor(p) {
    StringRef redirectionFile;
    // The first entry is the tty; no need to redirect in that case.
    if (m_MetaProcessor->m_PrevStdoutFileName.size()>1){
      redirectionFile = m_MetaProcessor->m_PrevStdoutFileName.back();
      redirect(STDOUT_FILENO, redirectionFile.str(), stdout);
    }
    if (m_MetaProcessor->m_PrevStderrFileName.size()>1){
      redirectionFile = m_MetaProcessor->m_PrevStderrFileName.back();
      redirect(STDERR_FILENO, redirectionFile.str(), stderr);
    }
  }

  void MetaProcessor::MaybeRedirectOutputRAII::redirect(int fd,
                                                    const std::string& fileName,
                                                    FILE* file) {
    if (!fileName.empty()) {
      FILE* redirectionFile = freopen(fileName.c_str(), "a", file);
      if (!redirectionFile) {
        llvm::errs()<<"cling::MetaProcessor Error: The file path is not valid.";
      } else {
        // TO DO: Improve the condition.
        m_isCurrentlyRedirecting = (fd == STDOUT_FILENO) ? kSTDOUT : kSTDERR;
      }
    }
  }

  void MetaProcessor::MaybeRedirectOutputRAII::pop() {
    SmallString<128> terminalName;
    if (m_isCurrentlyRedirecting & kSTDOUT) {
      if (!m_MetaProcessor->m_PrevStderrFileName.empty()) {
        terminalName = m_MetaProcessor->m_PrevStdoutFileName.front();
        unredirect(terminalName, stdout);
      }
    }
    if (m_isCurrentlyRedirecting & kSTDERR) {
      if (!m_MetaProcessor->m_PrevStderrFileName.empty()) {
        terminalName = m_MetaProcessor->m_PrevStderrFileName.front();
        unredirect(terminalName, stderr);
      }
    }
  }

  void MetaProcessor::MaybeRedirectOutputRAII::unredirect(
                                          llvm::SmallVectorImpl<char>& fileName,
                                          FILE* file) {
    // Switch back to previous file after line is processed.
    if (!fileName.empty()) {
      FILE* redirectionFile = freopen(fileName.data(), "w", file);
      if (!redirectionFile) {
        llvm::errs() << "cling::MetaProcessor::unredirect "
                     << "The redirection file path not valid "
                     << fileName.data() << ".\n";
      }
    }
  }


  MetaProcessor::MetaProcessor(Interpreter& interp, raw_ostream& outs) 
    : m_Interp(interp), m_Outs(outs) {
    m_InputValidator.reset(new InputValidator());
    m_MetaParser.reset(new MetaParser(new MetaSema(interp, *this)));
    m_PrevStdoutFileName.push_back(SmallString<128>());
    getTerminal(STDOUT_FILENO, m_PrevStdoutFileName.back());
    m_PrevStdoutFileName.back().push_back(0);
    m_PrevStdoutFileName.back().pop_back();
    m_PrevStderrFileName.push_back(SmallString<128>());
    getTerminal(STDERR_FILENO, m_PrevStderrFileName.back());
    m_PrevStderrFileName.back().push_back(0);
    m_PrevStderrFileName.back().pop_back();
  }

  MetaProcessor::~MetaProcessor() {}

  int MetaProcessor::process(const char* input_text,
                             Interpreter::CompilationResult& compRes,
                             StoredValueRef* result) {
    if (result)
      *result = StoredValueRef::invalidValue();
    compRes = Interpreter::kSuccess;
    int expectedIndent = m_InputValidator->getExpectedIndent();
    
    if (expectedIndent)
      compRes = Interpreter::kMoreInputExpected;
    if (!input_text || !input_text[0]) {
      // nullptr / empty string, nothing to do.
      return expectedIndent;
    }
    std::string input_line(input_text);
    if (input_line == "\n") { // just a blank line, nothing to do.
      return expectedIndent;
    }
    //  Check for and handle meta commands.
    m_MetaParser->enterNewInputLine(input_line);
    MetaSema::ActionResult actionResult = MetaSema::AR_Success;
    if (m_MetaParser->isMetaCommand(actionResult, result)) {

      if (m_MetaParser->isQuitRequested())
        return -1;

      if (actionResult != MetaSema::AR_Success)
        compRes = Interpreter::kFailure;
      return expectedIndent;
    }

    // Check if the current statement is now complete. If not, return to
    // prompt for more.
    if (m_InputValidator->validate(input_line) == InputValidator::kIncomplete) {
      compRes = Interpreter::kMoreInputExpected;
      return m_InputValidator->getExpectedIndent();
    }

    //  We have a complete statement, compile and execute it.
    std::string input = m_InputValidator->getInput();
    m_InputValidator->reset();
    // if (m_Options.RawInput)
    //   compResLocal = m_Interp.declare(input);
    // else
    compRes = m_Interp.process(input, result);

    return 0;
  }

  void MetaProcessor::cancelContinuation() const {
    m_InputValidator->reset();
  }

  int MetaProcessor::getExpectedIndent() const {
    return m_InputValidator->getExpectedIndent();
  }

  // Run a file: .x file[(args)]
  bool MetaProcessor::executeFile(llvm::StringRef file, llvm::StringRef args,
                                  Interpreter::CompilationResult& compRes,
                                  StoredValueRef* result) {
    // Look for start of parameters:
    typedef std::pair<llvm::StringRef,llvm::StringRef> StringRefPair;

    StringRefPair pairPathFile = file.rsplit('/');
    if (pairPathFile.second.empty()) {
       pairPathFile.second = pairPathFile.first;
    }
    StringRefPair pairFuncExt = pairPathFile.second.rsplit('.');

    Interpreter::CompilationResult interpRes = m_Interp.loadFile(file);
    if (interpRes == Interpreter::kSuccess) {
      std::string expression = pairFuncExt.first.str() + "(" + args.str() + ")";
      m_CurrentlyExecutingFile = file;
      bool topmost = !m_TopExecutingFile.data();
      if (topmost)
        m_TopExecutingFile = m_CurrentlyExecutingFile;
      
      interpRes = m_Interp.process(expression, result);

      m_CurrentlyExecutingFile = llvm::StringRef();
      if (topmost)
        m_TopExecutingFile = llvm::StringRef();
    }
    compRes = interpRes;
    return (interpRes != Interpreter::kFailure);
  }

  Interpreter::CompilationResult
  MetaProcessor::readInputFromFile(llvm::StringRef filename,
                                 StoredValueRef* result,
                                 bool ignoreOutmostBlock /*=false*/) {

    {
      // check that it's not binary:
      std::ifstream in(filename.str().c_str(), std::ios::in | std::ios::binary);
      char magic[1024] = {0};
      in.read(magic, sizeof(magic));
      size_t readMagic = in.gcount();
      if (readMagic >= 4) {
        llvm::StringRef magicStr(magic,in.gcount());
        llvm::sys::fs::file_magic fileType
          = llvm::sys::fs::identify_magic(magicStr);
        if (fileType != llvm::sys::fs::file_magic::unknown) {
          llvm::errs() << "Error in cling::MetaProcessor: "
            "cannot read input from a binary file!\n";
          return Interpreter::kFailure;
        }
        unsigned printable = 0;
        for (size_t i = 0; i < readMagic; ++i)
          if (isprint(magic[i]))
            ++printable;
        if (10 * printable <  5 * readMagic) {
          // 50% printable for ASCII files should be a safe guess.
          llvm::errs() << "Error in cling::MetaProcessor: "
            "cannot read input from a (likely) binary file!\n" << printable;
          return Interpreter::kFailure;
        }
      }
    }

    std::ifstream in(filename.str().c_str());
    in.seekg(0, std::ios::end);
    size_t size = in.tellg();
    std::string content(size, ' ');
    in.seekg(0);
    in.read(&content[0], size); 

    if (ignoreOutmostBlock && !content.empty()) {
      static const char whitespace[] = " \t\r\n";
      std::string::size_type posNonWS = content.find_first_not_of(whitespace);
      std::string::size_type replaced = posNonWS;
      if (posNonWS != std::string::npos) {
        if (content[posNonWS] == '{') {
          // hide the curly brace:
          content[posNonWS] = ' ';
          // and the matching closing '}'
          posNonWS = content.find_last_not_of(whitespace);
          if (posNonWS != std::string::npos) {
            if (content[posNonWS] == ';' && content[posNonWS-1] == '}') {
              content[posNonWS--] = ' '; // replace ';' and enter next if
            }
            if (content[posNonWS] == '}') {
              content[posNonWS] = ' '; // replace '}'
            } else {
              // More text (comments) are okay after the last '}', but
              // we can not easily find it to remove it (so we need to upgrade
              // this code to better handle the case with comments or
              // preprocessor code before and after the leading { and
              // trailing })
              content[replaced] = '{';
              // By putting the '{' back, we keep the code as consistent as
              // the user wrote it ... but we should still warn that we not 
              // goint to treat this file an unamed macro.
              llvm::errs() 
               << "Warning in cling::MetaProcessor: can not find the closing '}', "
               << llvm::sys::path::filename(filename)
               << " is not handled as an unamed script!\n";
            }
          } // find '}'
        } // have '{'
      } // have non-whitespace
    } // ignore outmost block

    std::string strFilename(filename.str());
    m_CurrentlyExecutingFile = strFilename;
    bool topmost = !m_TopExecutingFile.data();
    if (topmost)
      m_TopExecutingFile = m_CurrentlyExecutingFile;
    Interpreter::CompilationResult ret;
    if (process(content.c_str(), ret, result)) {
      // Input file has to be complete.
       llvm::errs() 
          << "Error in cling::MetaProcessor: file "
          << llvm::sys::path::filename(filename)
          << " is incomplete (missing parenthesis or similar)!\n";
      ret = Interpreter::kFailure;
    }
    m_CurrentlyExecutingFile = llvm::StringRef();
    if (topmost)
      m_TopExecutingFile = llvm::StringRef();
    return ret;
  }

  bool MetaProcessor::getTerminal(int fd,
                                  llvm::SmallVectorImpl<char>& out) {

    int ttyname_Result = ttyname_r(fd, const_cast<char*>(out.data()),
                                   out.capacity());
    while (ttyname_Result == ERANGE) {
      out.reserve(16*out.capacity());
      ttyname_Result = ttyname_r(fd, const_cast<char*>(out.data()),
                                 out.capacity());
    }

    if (ttyname_Result == 0) {
      out.set_size(strlen(out.data()));
      return true;
    } else if (ttyname_Result == EBADF) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: Bad file "
                   "descriptor " << fd << " .\n";
    } else if (ttyname_Result == ENOTTY) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: "
                   "File descriptor " << fd << " does not refer to a"
                   "terminal device.\n";
    } else if (ttyname_Result == EAGAIN) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: "
                   "The device driver was in use by another process, or the"
                   "driver was unable to carry out the request due to an"
                   "outstanding command in progress.\n";
    } else if (ttyname_Result == EINTR) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: "
                   "The function was interrupted by a signal.\n";
    } else if (ttyname_Result == ENOSYS) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: "
                   "The ttyname_r() function isn't implemented for the"
                   "filesystem specified by filedes " << fd << " .\n";
    } else if (ttyname_Result == EPERM) {
      llvm::errs() << "Error in cling::MetaProcessor::getTerminal: "
                   "The process doesn't have sufficient permission to carry"
                   "out the requested command.\n";
    }
    return false;
  }

  void MetaProcessor::setFileStream(llvm::StringRef file, bool append, int fd,
              llvm::SmallVector<llvm::SmallString<128>, 2>& prevFileStack) {
    // If we have a fileName to redirect to store it.
    if (!file.empty()) {
      prevFileStack.push_back(file);
      // pop and push a null terminating 0.
      // SmallVectorImpl<T> does not have a c_str(), thus instead of casting to
      // a SmallString<T> we null terminate the data that we have and pop the
      // 0 char back.
      prevFileStack.back().push_back(0);
      prevFileStack.back().pop_back();
      if (!append) {
        if (!fopen(file.data(), "w")) {
          llvm::errs() << "cling::MetaProcessor Error: The file path is not"
                       << " valid.";
        }
      }
    // Else unredirection, so switch to the previous file.
    } else {
      // If there is no previous file on the stack, we had stdout/stderr.
      if (prevFileStack.size()>1) {
        // Pop the file.
        prevFileStack.pop_back();
      }
    }
  }

  void MetaProcessor::setStdStream(llvm::StringRef file,
                                   RedirectionScope stream, bool append) {

    if (stream & kSTDOUT) {
      setFileStream(file, append, STDOUT_FILENO, m_PrevStdoutFileName);
    }
    if (stream & kSTDERR) {
      // Deal with the case 2>&1 and 2&>1
      if (strcmp(file.data(), "_IO_2_1_stdout_") == 0) {
        file = m_PrevStdoutFileName.front().str();
      }
      setFileStream(file, append, STDERR_FILENO, m_PrevStderrFileName);
    }
  }

} // end namespace cling
