#include "TClass.h"
#include "TInterpreter.h"
#include "TList.h"
#include "TMethod.h"

#include "gtest/gtest.h"

TEST(TClingMethodInfo, Prototype)
{
  TClass *cl = TClass::GetClass("TObject");
  ASSERT_NE(cl, nullptr);
  TMethod *meth = (TMethod*)cl->GetListOfMethods()->FindObject("SysError");
  ASSERT_NE(meth, nullptr);
  EXPECT_STREQ(meth->GetPrototype(), "void TObject::SysError(const char* method, const char* msgfmt,...) const");
}

TEST(TClingMethodInfo, ROOT10688)
{
  gInterpreter->Declare(R"CODE(
namespace ROOT10688 {
class Base {
protected:
  int protectedVar;
  void protectedFunc();
  void protectedFunc(float);

  int func();
public:
  int publicVar;
  void publicFunc();
  void publicFunc(int);

  Base(int);
};
class Derived: public Base {
protected:
  using Base::func;
public:
  Derived() = delete;
  Derived(float);
  using Base::Base;

  using Base::protectedVar;
  using Base::publicVar;
  using Base::protectedFunc;
  using Base::publicFunc;
};
}
)CODE");

  TClass *cl = TClass::GetClass("ROOT10688::Derived");
  ASSERT_NE(cl, nullptr);
  TMethod *meth = (TMethod*)cl->GetListOfMethods()->FindObject("func");
  ASSERT_NE(meth, nullptr);
}
