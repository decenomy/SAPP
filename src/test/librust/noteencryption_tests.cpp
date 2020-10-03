// Copyright (c) 2016-2020 The ZCash developers
// Copyright (c) 2020 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test/test_pivx.h"

#include <array>
#include <stdexcept>

#include "sapling/address.hpp"
#include "sapling/note.hpp"
#include "sapling/noteencryption.hpp"
#include "sapling/prf.h"
#include "sapling/uint252.h"
#include "sapling/sapling_util.h"
#include "crypto/sha256.h"

#include <boost/test/unit_test.hpp>
#include <librustzcash.h>
#include <sodium.h>

BOOST_FIXTURE_TEST_SUITE(noteencryption_tests, BasicTestingSetup)

class TestNoteDecryption : public ZCNoteDecryption {
public:
    TestNoteDecryption(uint256 sk_enc) : ZCNoteDecryption(sk_enc) {}

    void change_pk_enc(uint256 to) {
        pk_enc = to;
    }
};

BOOST_AUTO_TEST_CASE(note_plain_text_test)
{
    auto xsk = libzcash::SaplingSpendingKey(uint256()).expanded_spending_key();
    auto fvk = xsk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::SaplingPaymentAddress addr = *ivk.address({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    std::array<unsigned char, ZC_MEMO_SIZE> memo;
    for (size_t i = 0; i < ZC_MEMO_SIZE; i++) {
        // Fill the message with dummy data
        memo[i] = (unsigned char) i;
    }

    libzcash::SaplingNote note(addr, 39393);
    auto cmu_opt = note.cmu();
    if (!cmu_opt) {
        BOOST_ERROR("SaplingNote cm failed");
    }
    uint256 cmu = cmu_opt.get();
    libzcash::SaplingNotePlaintext pt(note, memo);

    auto res = pt.encrypt(addr.pk_d);
    if (!res) {
        BOOST_ERROR("SaplingNotePlaintext encrypt failed");
    }

    auto enc = res.get();

    auto ct = enc.first;
    auto encryptor = enc.second;
    auto epk = encryptor.get_epk();

    // Try to decrypt with incorrect commitment
    BOOST_CHECK(!libzcash::SaplingNotePlaintext::decrypt(
        ct,
        ivk,
        epk,
        uint256()
    ));

    // Try to decrypt with correct commitment
    auto foo = libzcash::SaplingNotePlaintext::decrypt(
        ct,
        ivk,
        epk,
        cmu
    );

    if (!foo) {
        BOOST_ERROR("SaplingNotePlaintext decrypt failed");
    }

    auto bar = foo.get();

    BOOST_CHECK(bar.value() == pt.value());
    BOOST_CHECK(bar.memo() == pt.memo());
    BOOST_CHECK(bar.d == pt.d);
    BOOST_CHECK(bar.rcm == pt.rcm);

    auto foobar = bar.note(ivk);

    if (!foobar) {
        // Improve this message
        BOOST_ERROR("Invalid note");
    }

    auto new_note = foobar.get();

    BOOST_CHECK(note.value() == new_note.value());
    BOOST_CHECK(note.d == new_note.d);
    BOOST_CHECK(note.pk_d == new_note.pk_d);
    BOOST_CHECK(note.r == new_note.r);
    BOOST_CHECK(note.cmu() == new_note.cmu());

    libzcash::SaplingOutgoingPlaintext out_pt;
    out_pt.pk_d = note.pk_d;
    out_pt.esk = encryptor.get_esk();

    auto ovk = random_uint256();
    auto cv = random_uint256();
    auto cm = random_uint256();

    auto out_ct = out_pt.encrypt(
        ovk,
        cv,
        cm,
        encryptor
    );

    auto decrypted_out_ct = out_pt.decrypt(
        out_ct,
        ovk,
        cv,
        cm,
        encryptor.get_epk()
    );

    if (!decrypted_out_ct) {
        BOOST_ERROR("SaplingOutgoingPlaintext decrypt failed");
    }

    auto decrypted_out_ct_unwrapped = decrypted_out_ct.get();

    BOOST_CHECK(decrypted_out_ct_unwrapped.pk_d == out_pt.pk_d);
    BOOST_CHECK(decrypted_out_ct_unwrapped.esk == out_pt.esk);

    // Test sender won't accept invalid commitments
    BOOST_CHECK(!libzcash::SaplingNotePlaintext::decrypt(
        ct,
        epk,
        decrypted_out_ct_unwrapped.esk,
        decrypted_out_ct_unwrapped.pk_d,
        uint256()
    ));

    // Test sender can decrypt the note ciphertext.
    foo = libzcash::SaplingNotePlaintext::decrypt(
        ct,
        epk,
        decrypted_out_ct_unwrapped.esk,
        decrypted_out_ct_unwrapped.pk_d,
        cmu
    );

    if (!foo) {
        BOOST_ERROR("Sender decrypt note ciphertext failed.");
    }

    bar = foo.get();

    BOOST_CHECK(bar.value() == pt.value());
    BOOST_CHECK(bar.memo() == pt.memo());
    BOOST_CHECK(bar.d == pt.d);
    BOOST_CHECK(bar.rcm == pt.rcm);
}

BOOST_AUTO_TEST_CASE(SaplingApi_test)
{
    // Create recipient addresses
    auto sk = libzcash::SaplingSpendingKey(uint256()).expanded_spending_key();
    auto vk = sk.full_viewing_key();
    auto ivk = vk.in_viewing_key();
    libzcash::SaplingPaymentAddress pk_1 = *ivk.address({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    libzcash::SaplingPaymentAddress pk_2 = *ivk.address({4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});

    // Blob of stuff we're encrypting
    std::array<unsigned char, ZC_SAPLING_ENCPLAINTEXT_SIZE> message;
    for (size_t i = 0; i < ZC_SAPLING_ENCPLAINTEXT_SIZE; i++) {
    // Fill the message with dummy data
    message[i] = (unsigned char) i;
    }

    std::array<unsigned char, ZC_SAPLING_OUTPLAINTEXT_SIZE> small_message;
    for (size_t i = 0; i < ZC_SAPLING_OUTPLAINTEXT_SIZE; i++) {
    // Fill the message with dummy data
    small_message[i] = (unsigned char) i;
    }

    // Invalid diversifier
    BOOST_CHECK(boost::none == libzcash::SaplingNoteEncryption::FromDiversifier({1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));

    // Encrypt to pk_1
    auto enc = *libzcash::SaplingNoteEncryption::FromDiversifier(pk_1.d);
    auto ciphertext_1 = *enc.encrypt_to_recipient(
            pk_1.pk_d,
            message
    );
    auto epk_1 = enc.get_epk();
    {
        uint256 test_epk;
        uint256 test_esk = enc.get_esk();
        BOOST_CHECK(librustzcash_sapling_ka_derivepublic(pk_1.d.begin(), test_esk.begin(), test_epk.begin()));
        BOOST_CHECK(test_epk == epk_1);
    }
    auto cv_1 = random_uint256();
    auto cm_1 = random_uint256();
    auto out_ciphertext_1 = enc.encrypt_to_ourselves(
            sk.ovk,
            cv_1,
            cm_1,
            small_message
    );

    // Encrypt to pk_2
    enc = *libzcash::SaplingNoteEncryption::FromDiversifier(pk_2.d);
    auto ciphertext_2 = *enc.encrypt_to_recipient(
            pk_2.pk_d,
            message
    );
    auto epk_2 = enc.get_epk();

    auto cv_2 = random_uint256();
    auto cm_2 = random_uint256();
    auto out_ciphertext_2 = enc.encrypt_to_ourselves(
            sk.ovk,
            cv_2,
            cm_2,
            small_message
    );

    // Test nonce-reuse resistance of API
    {
        auto tmp_enc = *libzcash::SaplingNoteEncryption::FromDiversifier(pk_1.d);

        tmp_enc.encrypt_to_recipient(
                pk_1.pk_d,
                message
                );

        BOOST_CHECK_THROW(tmp_enc.encrypt_to_recipient(
                pk_1.pk_d,
                message
                ), std::logic_error);

        tmp_enc.encrypt_to_ourselves(
                sk.ovk,
                cv_2,
                cm_2,
                small_message
                );

        BOOST_CHECK_THROW(tmp_enc.encrypt_to_ourselves(
                sk.ovk,
                cv_2,
                cm_2,
                small_message
                ), std::logic_error);
    }

    // Try to decrypt
    auto plaintext_1 = *AttemptSaplingEncDecryption(
            ciphertext_1,
            ivk,
            epk_1
    );
    BOOST_CHECK(message == plaintext_1);

    auto small_plaintext_1 = *libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_1,
            sk.ovk,
            cv_1,
            cm_1,
            epk_1
    );
    BOOST_CHECK(small_message == small_plaintext_1);

    auto plaintext_2 = *AttemptSaplingEncDecryption(
            ciphertext_2,
            ivk,
            epk_2
    );
    BOOST_CHECK(message == plaintext_2);

    auto small_plaintext_2 = *libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_2,
            sk.ovk,
            cv_2,
            cm_2,
            epk_2
    );
    BOOST_CHECK(small_message == small_plaintext_2);

    // Try to decrypt out ciphertext with wrong key material
    BOOST_CHECK(!libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_1,
            random_uint256(),
            cv_1,
            cm_1,
            epk_1
    ));
    BOOST_CHECK(!libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_1,
            sk.ovk,
            random_uint256(),
            cm_1,
            epk_1
    ));
    BOOST_CHECK(!libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_1,
            sk.ovk,
            cv_1,
            random_uint256(),
            epk_1
    ));
    BOOST_CHECK(!libzcash::AttemptSaplingOutDecryption(
            out_ciphertext_1,
            sk.ovk,
            cv_1,
            cm_1,
            random_uint256()
    ));

    // Try to decrypt with wrong ephemeral key
    BOOST_CHECK(!AttemptSaplingEncDecryption(
            ciphertext_1,
            ivk,
            epk_2
    ));
    BOOST_CHECK(!AttemptSaplingEncDecryption(
            ciphertext_2,
            ivk,
            epk_1
    ));

    // Try to decrypt with wrong ivk
    BOOST_CHECK(!libzcash::AttemptSaplingEncDecryption(
            ciphertext_1,
            uint256(),
            epk_1
    ));
    BOOST_CHECK(!libzcash::AttemptSaplingEncDecryption(
            ciphertext_2,
            uint256(),
            epk_2
    ));
}

BOOST_AUTO_TEST_CASE(api_test)
{
    uint256 sk_enc = ZCNoteEncryption::generate_privkey(uint252(uint256S("21035d60bc1983e37950ce4803418a8fb33ea68d5b937ca382ecbae7564d6a07")));
    uint256 pk_enc = ZCNoteEncryption::generate_pubkey(sk_enc);

    ZCNoteEncryption b = ZCNoteEncryption(uint256());
    for (size_t i = 0; i < 100; i++) {
        ZCNoteEncryption c = ZCNoteEncryption(uint256());
        BOOST_CHECK(b.get_epk() != c.get_epk());
    }

    std::array<unsigned char, ZC_NOTEPLAINTEXT_SIZE> message;
    for (size_t i = 0; i < ZC_NOTEPLAINTEXT_SIZE; i++) {
        // Fill the message with dummy data
        message[i] = (unsigned char) i;
    }

    for (int i = 0; i < 255; i++) {
        auto ciphertext = b.encrypt(pk_enc, message);

        {
            ZCNoteDecryption decrypter(sk_enc);

            // Test decryption
            auto plaintext = decrypter.decrypt(ciphertext, b.get_epk(), uint256(), i);
            BOOST_CHECK(plaintext == message);

            // Test wrong nonce
            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, b.get_epk(), uint256(), (i == 0) ? 1 : (i - 1)),
            libzcash::note_decryption_failed);

            // Test wrong ephemeral key
            {
            ZCNoteEncryption c = ZCNoteEncryption(uint256());

            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, c.get_epk(), uint256(), i),
            libzcash::note_decryption_failed);
            }

            // Test wrong seed
            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, b.get_epk(), uint256S("11035d60bc1983e37950ce4803418a8fb33ea68d5b937ca382ecbae7564d6a77"), i),
            libzcash::note_decryption_failed);

            // Test corrupted ciphertext
            ciphertext[10] ^= 0xff;
            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, b.get_epk(), uint256(), i),
            libzcash::note_decryption_failed);
            ciphertext[10] ^= 0xff;
        }

        {
            // Test wrong private key
            uint256 sk_enc_2 = ZCNoteEncryption::generate_privkey(uint252());
            ZCNoteDecryption decrypter(sk_enc_2);

            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, b.get_epk(), uint256(), i),
            libzcash::note_decryption_failed);
        }

        {
            TestNoteDecryption decrypter(sk_enc);

            // Test decryption
            auto plaintext = decrypter.decrypt(ciphertext, b.get_epk(), uint256(), i);
            BOOST_CHECK(plaintext == message);

            // Test wrong public key (test of KDF)
            decrypter.change_pk_enc(uint256());
            BOOST_CHECK_THROW(decrypter.decrypt(ciphertext, b.get_epk(), uint256(), i),
            libzcash::note_decryption_failed);
        }
    }

    // Nonce space should run out here
    try {
        b.encrypt(pk_enc, message);
        BOOST_FAIL("Expected std::logic_error");
    } catch(std::logic_error const & err) {
        BOOST_CHECK(err.what() == std::string("no additional nonce space for KDF"));
    } catch(...) {
        BOOST_FAIL("Expected std::logic_error");
    }
}

uint256 test_prf(
        unsigned char distinguisher,
        uint252 seed_x,
        uint256 y
) {
    uint256 x = seed_x.inner();
    *x.begin() &= 0x0f;
    *x.begin() |= distinguisher;
    CSHA256 hasher;
    hasher.Write(x.begin(), 32);
    hasher.Write(y.begin(), 32);

    uint256 ret;
    hasher.FinalizeNoPadding(ret.begin());
    return ret;
}

BOOST_AUTO_TEST_CASE(PrfAddr_test)
{
    for (size_t i = 0; i < 100; i++) {
        uint252 a_sk = random_uint252();
        uint256 rest;
        BOOST_CHECK(
                test_prf(0xc0, a_sk, rest) == PRF_addr_a_pk(a_sk)
        );
    }

    for (size_t i = 0; i < 100; i++) {
        uint252 a_sk = random_uint252();
        uint256 rest;
        *rest.begin() = 0x01;
        BOOST_CHECK(
                test_prf(0xc0, a_sk, rest) == PRF_addr_sk_enc(a_sk)
        );
    }
}

BOOST_AUTO_TEST_CASE(PrfNf_test)
{
    for (size_t i = 0; i < 100; i++) {
        uint252 a_sk = random_uint252();
        uint256 rho = random_uint256();
        BOOST_CHECK(
                test_prf(0xe0, a_sk, rho) == PRF_nf(a_sk, rho)
        );
    }
}

BOOST_AUTO_TEST_CASE(uint252_test)
{
    BOOST_CHECK_THROW(uint252(uint256S("f6da8716682d600f74fc16bd0187faad6a26b4aa4c24d5c055b216d94516847e")), std::domain_error);
}

BOOST_AUTO_TEST_SUITE_END()
