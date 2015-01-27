#include "mozilla/ModuleUtils.h"
#include "nsIClassInfoImpl.h"

#include "nsTest.h"

////////////////////////////////////////////////////////////////////////
// With the below sample, you can define an implementation glue
// that talks with xpcom for creation of component nsTestImpl
// that implement the interface nsITest. This can be extended for
// any number of components.
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// Define the contructor function for the object nsTestImpl
//
// What this does is defines a function nsTestImplConstructor which we
// will specific in the nsModuleComponentInfo table. This function will
// be used by the generic factory to create an instance of nsTestImpl.
//
// NOTE: This creates an instance of nsTestImpl by using the default
//         constructor nsTestImpl::nsTestImpl()
//
NS_GENERIC_FACTORY_CONSTRUCTOR(nsTest)

// The following line defines a kNS_TEST_CID CID variable.
NS_DEFINE_NAMED_CID(NS_TEST_CID);

// Build a table of ClassIDs (CIDs) which are implemented by this module. CIDs
// should be completely unique UUIDs.
// each entry has the form { CID, service, factoryproc, constructorproc }
// where factoryproc is usually nullptr.
static const mozilla::Module::CIDEntry kTestCIDs[] = {
  { &kNS_TEST_CID, false, nullptr, nsTestConstructor },
  { nullptr }
};

// Build a table which maps contract IDs to CIDs.
// A contract is a string which identifies a particular set of functionality. In some
// cases an extension component may override the contract ID of a builtin gecko component
// to modify or extend functionality.
static const mozilla::Module::ContractIDEntry kTestContracts[] = {
  { NS_TEST_CONTRACTID, &kNS_TEST_CID },
  { nullptr }
};

// Category entries are category/key/value triples which can be used
// to register contract ID as content handlers or to observe certain
// notifications. Most modules do not need to register any category
// entries: this is just a sample of how you'd do it.
// @see nsICategoryManager for information on retrieving category data.
static const mozilla::Module::CategoryEntry kTestCategories[] = {
  { "my-category", "my-key", NS_TEST_CONTRACTID },
  { nullptr }
};

static const mozilla::Module kTestModule = {
  mozilla::Module::kVersion,
  kTestCIDs,
  kTestContracts,
  kTestCategories
};

// The following line implements the one-and-only "NSModule" symbol exported from this
// shared library.
NSMODULE_DEFN(nsTestModule) = &kTestModule;

// The following line implements the one-and-only "NSGetModule" symbol
// for compatibility with mozilla 1.9.2. You should only use this
// if you need a binary which is backwards-compatible and if you use
// interfaces carefully across multiple versions.
NS_IMPL_MOZILLA192_NSGETMODULE(&kTestModule)