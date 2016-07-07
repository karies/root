//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Bianca-Cristina Cristescu <bianca-cristina.cristescu@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef TABCOMPLETION_H
#define TABCOMPLETION_H

#include "textinput/Callbacks.h"

namespace textinput {
	class TabCompletion;
	class Text;
	class EditorRange;
}	

namespace cling {
  class TabCompletion : public textinput::TabCompletion {
    const cling::Interpreter& m_ParentInterpreter;
  
  public:
    TabCompletion(cling::Interpreter& Parent) : m_ParentInterpreter(Parent) {}
    ~TabCompletion() {}

    bool Complete(textinput::Text& Line /*in+out*/,
                size_t& Cursor /*in+out*/,
                textinput::EditorRange& R /*out*/,
                std::vector<std::string>& DisplayCompletions /*out*/) override {
      m_ParentInterpreter->CodeComplete(Line.GetText(), Cursor,
                                        DisplayCompletions);
    }
  };
}

#endif
