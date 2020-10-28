// Copyright (c) 2016-2020 The ZCash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/test/wallet_test_fixture.h"
#include "wallet/wallet.h"

#include "rpc/server.h"
#include "rpc/client.h"

#include "sapling/key_io_sapling.h"
#include "sapling/address.hpp"

#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include <univalue.h>

extern UniValue CallRPC(std::string args); // Implemented in rpc_tests.cpp

// Remember: this method will be moved to an utility file in the short future. For now, it's in sapling_keystore_tests.cpp
extern libzcash::SaplingExtendedSpendingKey GetTestMasterSaplingSpendingKey();

namespace {

    /** Set the working directory for the duration of the scope. */
    class PushCurrentDirectory {
    public:
        PushCurrentDirectory(const std::string &new_cwd)
                : old_cwd(boost::filesystem::current_path()) {
            boost::filesystem::current_path(new_cwd);
        }

        ~PushCurrentDirectory() {
            boost::filesystem::current_path(old_cwd);
        }
    private:
        boost::filesystem::path old_cwd;
    };

}

BOOST_FIXTURE_TEST_SUITE(sapling_rpc_wallet_tests, WalletTestingSetup)

/**
 * This test covers RPC command validateaddress
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_sapling_validateaddress)
{
    SelectParams(CBaseChainParams::MAIN);
    UniValue retValue;

    // Check number of args
    BOOST_CHECK_THROW(CallRPC("validateaddress"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("validateaddress toomany args"), std::runtime_error);

    // Wallet should be empty:
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size()==0);

    // This Sapling address is not valid, it belongs to another network
    BOOST_CHECK_NO_THROW(retValue = CallRPC("validateaddress ptestsapling1nrn6exksuqtpld9gu6fwdz4hwg54h2x37gutdds89pfyg6mtjf63km45a8eare5qla45cj75vs8"));
    UniValue resultObj = retValue.get_obj();
    bool b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // This Sapling address is valid, but the spending key is not in this wallet
    BOOST_CHECK_NO_THROW(retValue = CallRPC("validateaddress ps1u87kylcmn28yclnx2uy0psnvuhs2xn608ukm6n2nshrpg2nzyu3n62ls8j77m9cgp40dx40evej"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "type").get_str(), "sapling");
    b = find_value(resultObj, "ismine").get_bool();
    BOOST_CHECK_EQUAL(b, false);
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifier").get_str(), "e1fd627f1b9a8e4c7e6657");
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifiedtransmissionkey").get_str(), "d35e0d0897edbd3cf02b3d2327622a14c685534dbd2d3f4f4fa3e0e56cc2f008");
}

BOOST_AUTO_TEST_CASE(rpc_wallet_sapling_importkey_paymentaddress) {
    SelectParams(CBaseChainParams::MAIN);
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->SetMinVersion(FEATURE_SAPLING);
        pwalletMain->SetupSPKM(false);
    }

    auto testAddress = [](const std::string& key) {
        UniValue ret;
        BOOST_CHECK_NO_THROW(ret = CallRPC("importsaplingkey " + key));
        auto defaultAddr = find_value(ret, "address").get_str();
        BOOST_CHECK_NO_THROW(ret = CallRPC("validateaddress " + defaultAddr));
        ret = ret.get_obj();
        BOOST_CHECK_EQUAL(true, find_value(ret, "isvalid").get_bool());
        BOOST_CHECK_EQUAL(true, find_value(ret, "ismine").get_bool());
    };

    testAddress("p-secret-spending-key-main1qv09u0wlqqqqpqp75kpmat6l3ce29k"
                "g9half9epsm80wya5n92j4d8mtmesrukzxlsmm2f74v3nvvx2shxy4z5v5x39p"
                "eelsy5y2uxmvadaku8crd20q6vt8cvd68wp08cjyec6cku0dcf5lc9c2kykg5c"
                "8uqmqlx8ccxpsw7ae243quhwr0zyekrrc520gs9z0j8pm954c3cev2yvp29vrc"
                "0zweu7stxkwhp593p6drheps9uhz9pvkrfgvpxzte8d60uzw0qxadnsc77tcd");

}

/*
 * This test covers RPC commands listsaplingaddresses, importsaplingkey, exportsaplingkey
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_sapling_importexport)
{
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->SetMinVersion(FEATURE_SAPLING);
        pwalletMain->SetupSPKM(false);
    }
    UniValue retValue;
    int n1 = 1000; // number of times to import/export
    int n2 = 1000; // number of addresses to create and list

    // error if no args
    BOOST_CHECK_THROW(CallRPC("importsaplingkey"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("exportsaplingkey"), std::runtime_error);

    // error if too many args
    BOOST_CHECK_THROW(CallRPC("importsaplingkey way too many args"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("exportsaplingkey toomany args"), std::runtime_error);

    // error if invalid args
    auto sk = libzcash::SproutSpendingKey::random();
    std::string prefix = std::string("importsaplingkey ") + KeyIO::EncodeSpendingKey(sk) + " yes ";
    BOOST_CHECK_THROW(CallRPC(prefix + "-1"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC(prefix + "2147483647"), std::runtime_error); // allowed, but > height of active chain tip
    BOOST_CHECK_THROW(CallRPC(prefix + "2147483648"), std::runtime_error); // not allowed, > int32 used for nHeight
    BOOST_CHECK_THROW(CallRPC(prefix + "100badchars"), std::runtime_error);

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> saplingAddrs;
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK(saplingAddrs.empty());

    auto m = GetTestMasterSaplingSpendingKey();

    // verify import and export key
    for (int i = 0; i < n1; i++) {
        // create a random Sapling key locally
        auto testSaplingSpendingKey = m.Derive(i);
        auto testSaplingPaymentAddress = testSaplingSpendingKey.DefaultAddress();
        std::string testSaplingAddr = KeyIO::EncodePaymentAddress(testSaplingPaymentAddress);
        std::string testSaplingKey = KeyIO::EncodeSpendingKey(testSaplingSpendingKey);
        BOOST_CHECK_NO_THROW(CallRPC(std::string("importsaplingkey ") + testSaplingKey));
        BOOST_CHECK_NO_THROW(retValue = CallRPC(std::string("exportsaplingkey ") + testSaplingAddr));
        BOOST_CHECK_EQUAL(retValue.get_str(), testSaplingKey);
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("listshieldedaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK((int) arr.size() == n1);

    // Put addresses into a set
    std::unordered_set<std::string> myaddrs;
    for (const UniValue& element : arr.getValues()) {
        myaddrs.insert(element.get_str());
    }

    // Make new addresses for the set
    for (int i=0; i<n2; i++) {
        myaddrs.insert(KeyIO::EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey()));
    }

    // Verify number of addresses stored in wallet is n1+n2
    int numAddrs = myaddrs.size();
    BOOST_CHECK(numAddrs == n1 + n2);
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK((int) saplingAddrs.size() == numAddrs);

    // Ask wallet to list addresses
    BOOST_CHECK_NO_THROW(retValue = CallRPC("listshieldedaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK((int) arr.size() == numAddrs);

    // Create a set from them
    std::unordered_set<std::string> listaddrs;
    for (const UniValue& element : arr.getValues()) {
        listaddrs.insert(element.get_str());
    }

    // Verify the two sets of addresses are the same
    BOOST_CHECK((int) listaddrs.size() == numAddrs);
    BOOST_CHECK(myaddrs == listaddrs);

}

// Check if address is of given type and spendable from our wallet.
void CheckHaveAddr(const libzcash::PaymentAddress& addr) {

    BOOST_CHECK(IsValidPaymentAddress(addr));
    auto addr_of_type = boost::get<libzcash::SaplingPaymentAddress>(&addr);
    BOOST_ASSERT(addr_of_type != nullptr);
    BOOST_CHECK(pwalletMain->HaveSpendingKeyForPaymentAddress(*addr_of_type));
}

BOOST_AUTO_TEST_CASE(rpc_wallet_getnewshieldedaddress) {
    UniValue addr;
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->SetMinVersion(FEATURE_SAPLING);
        pwalletMain->SetupSPKM(false);
    }

    // No parameter defaults to sapling address
    addr = CallRPC("getnewshieldedaddress");
    CheckHaveAddr(KeyIO::DecodePaymentAddress(addr.get_str()));
    // Too many arguments will throw with the help
    BOOST_CHECK_THROW(CallRPC("getnewshieldedaddress many args"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_wallet_encrypted_wallet_sapzkeys)
{
    UniValue retValue;
    int n = 100;

    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->SetMinVersion(FEATURE_SAPLING);
        pwalletMain->SetupSPKM(false);
    }

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    BOOST_CHECK(addrs.empty());

    // create keys
    for (int i = 0; i < n; i++) {
        CallRPC("getnewshieldedaddress");
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("listshieldedaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK((int) arr.size() == n);

    // Verify that the wallet encryption RPC is disabled
    // TODO: We don't have the experimental mode to disable the encryptwallet disable.
    //BOOST_CHECK_THROW(CallRPC("encryptwallet passphrase"), std::runtime_error);

    // Encrypt the wallet (we can't call RPC encryptwallet as that shuts down node)
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";

    PushCurrentDirectory push_dir(gArgs.GetArg("-datadir","/tmp/thisshouldnothappen"));
    BOOST_CHECK(pwalletMain->EncryptWallet(strWalletPass));

    // Verify we can still list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("listshieldedaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK((int) arr.size() == n);

    // Try to add a new key, but we can't as the wallet is locked
    BOOST_CHECK_THROW(CallRPC("getnewshieldedaddress"), std::runtime_error);

    // We can't call RPC walletpassphrase as that invokes RPCRunLater which breaks tests.
    // So we manually unlock.
    BOOST_CHECK(pwalletMain->Unlock(strWalletPass));

    // Now add a key
    BOOST_CHECK_NO_THROW(CallRPC("getnewshieldedaddress"));

    // Verify the key has been added
    BOOST_CHECK_NO_THROW(retValue = CallRPC("listshieldedaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK((int) arr.size() == n+1);

    // We can't simulate over RPC the wallet closing and being reloaded
    // but there are tests for this in gtest.
}

BOOST_AUTO_TEST_SUITE_END()
