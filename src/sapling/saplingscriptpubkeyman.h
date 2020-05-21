// Copyright (c) 2020 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_SAPLINGSCRIPTPUBKEYMAN_H
#define PIVX_SAPLINGSCRIPTPUBKEYMAN_H

#include "consensus/consensus.h"
#include "wallet/hdchain.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "sapling/incrementalmerkletree.hpp"

//! Size of witness cache
//  Should be large enough that we can expect not to reorg beyond our cache
//  unless there is some exceptional network disruption.
static const unsigned int WITNESS_CACHE_SIZE = DEFAULT_MAX_REORG_DEPTH + 1;

class CBlock;
class CBlockIndex;

class SaplingNoteData
{
public:

    SaplingNoteData() : nullifier() { }
    SaplingNoteData(const libzcash::SaplingIncomingViewingKey& _ivk) : ivk {_ivk}, nullifier() { }
    SaplingNoteData(const libzcash::SaplingIncomingViewingKey& _ivk, const uint256& n) : ivk {_ivk}, nullifier(n) { }

    std::list<SaplingWitness> witnesses;
    libzcash::SaplingIncomingViewingKey ivk;

    /**
     * Block height corresponding to the most current witness.
     *
     * When we first create a SaplingNoteData in SaplingScriptPubKeyMan::FindMySaplingNotes, this is set to
     * -1 as a placeholder. The next time CWallet::ChainTip is called, we can
     * determine what height the witness cache for this note is valid for (even
     * if no witnesses were cached), and so can set the correct value in
     * SaplingScriptPubKeyMan::IncrementNoteWitnesses and SaplingScriptPubKeyMan::DecrementNoteWitnesses.
     */
    int witnessHeight{-1};

    /**
     * Cached note nullifier. May not be set if the wallet was not unlocked when
     * this SaplingNoteData was created. If not set, we always assume that the
     * note has not been spent.
     *
     * It's okay to cache the nullifier in the wallet, because we are storing
     * the spending key there too, which could be used to derive this.
     * If the wallet is encrypted, this means that someone with access to the
     * locked wallet cannot spend notes, but can connect received notes to the
     * transactions they are spent in. This is the same security semantics as
     * for transparent addresses.
     */
    Optional<uint256> nullifier;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(nVersion);
        }
        READWRITE(ivk);
        READWRITE(nullifier);
        READWRITE(witnesses);
        READWRITE(witnessHeight);
    }

    friend bool operator==(const SaplingNoteData& a, const SaplingNoteData& b) {
        return (a.ivk == b.ivk && a.nullifier == b.nullifier && a.witnessHeight == b.witnessHeight);
    }

    friend bool operator!=(const SaplingNoteData& a, const SaplingNoteData& b) {
        return !(a == b);
    }
};

enum KeyAddResult {
    SpendingKeyExists,
    KeyAlreadyExists,
    KeyAdded,
    KeyNotAdded,
};

typedef std::map<SaplingOutPoint, SaplingNoteData> mapSaplingNoteData_t;

/*
 * Sapling keys manager
 * todo: add real description..
 */
class SaplingScriptPubKeyMan {

public:
    SaplingScriptPubKeyMan(CWallet *parent) : wallet(parent) {}

    ~SaplingScriptPubKeyMan() {};

    /**
     * Keep track of the used nullifier.
     */
    void AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid);
    bool IsSaplingSpent(const uint256& nullifier) const;

    /**
     * pindex is the new tip being connected.
     */
    void IncrementNoteWitnesses(const CBlockIndex* pindex,
                                const CBlock* pblock,
                                SaplingMerkleTree& saplingTree);
    /**
     * pindex is the old tip being disconnected.
     */
    void DecrementNoteWitnesses(const CBlockIndex* pindex);

    /**
     * Update mapSaplingNullifiersToNotes
     * with the cached nullifiers in this tx.
     */
    void UpdateNullifierNoteMapWithTx(const CWalletTx& wtx);

    /**
     *  Update mapSaplingNullifiersToNotes, computing the nullifier
     *  from a cached witness if necessary.
     */
    void UpdateSaplingNullifierNoteMapWithTx(CWalletTx& wtx);

    /**
     * Iterate over transactions in a block and update the cached Sapling nullifiers
     * for transactions which belong to the wallet.
     */
    void UpdateSaplingNullifierNoteMapForBlock(const CBlock* pblock);

    /**
     * Set and initialize the Sapling HD chain.
     */
    bool SetupGeneration(const CKeyID& keyID, bool force = false);
    bool IsEnabled() const { return !hdChain.IsNull(); };

    /* Set the current HD seed (will reset the chain child index counters)
      Sets the seed's version based on the current wallet version (so the
      caller must ensure the current wallet version is correct before calling
      this function). */
    void SetHDSeed(const CPubKey& key, bool force = false, bool memonly = false);
    void SetHDSeed(const CKeyID& keyID, bool force = false, bool memonly = false);

    /* Set the HD chain model (chain child index counters) */
    void SetHDChain(CHDChain& chain, bool memonly);
    const CHDChain& GetHDChain() const { return hdChain; }

    /* Encrypt Sapling keys */
    bool EncryptSaplingKeys(CKeyingMaterial& vMasterKeyIn);

    void GetConflicts(const CWalletTx& wtx, std::set<uint256>& result) const;

    // Add full viewing key if it's not already in the wallet
    KeyAddResult AddViewingKeyToWallet(const libzcash::SaplingExtendedFullViewingKey &extfvk) const;

    // Add spending key if it's not already in the wallet
    KeyAddResult AddSpendingKeyToWallet(
            const Consensus::Params &params,
            const libzcash::SaplingExtendedSpendingKey &sk,
            int64_t nTime);

    //! Generates new Sapling key
    libzcash::SaplingPaymentAddress GenerateNewSaplingZKey();
    //! Adds Sapling spending key to the store, and saves it to disk
    bool AddSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key);
    bool AddSaplingIncomingViewingKey(
            const libzcash::SaplingIncomingViewingKey &ivk,
            const libzcash::SaplingPaymentAddress &addr);
    bool AddCryptedSaplingSpendingKeyDB(
            const libzcash::SaplingExtendedFullViewingKey &extfvk,
            const std::vector<unsigned char> &vchCryptedSecret);
    //! Returns true if the wallet contains the spending key
    bool HaveSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &zaddr) const;

    //! Adds spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key);
    //! Load spending key metadata (used by LoadWallet)
    bool LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta);
    //! Adds an encrypted spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedSaplingZKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                                const std::vector<unsigned char> &vchCryptedSecret);
    //! Adds a Sapling payment address -> incoming viewing key map entry,
    //! without saving it to disk (used by LoadWallet)
    bool LoadSaplingPaymentAddress(
            const libzcash::SaplingPaymentAddress &addr,
            const libzcash::SaplingIncomingViewingKey &ivk);
    bool AddSaplingSpendingKey(const libzcash::SaplingExtendedSpendingKey &sk);

    //! Return the full viewing key for the shielded address
    Optional<libzcash::SaplingExtendedFullViewingKey> GetViewingKeyForPaymentAddress(
            const libzcash::SaplingPaymentAddress &addr) const;

    //! Return the spending key for the payment address (nullopt if the wallet has no spending key for such address)
    Optional<libzcash::SaplingExtendedSpendingKey> GetSpendingKeyForPaymentAddress(const libzcash::SaplingPaymentAddress &addr) const;

    //! Finds all output notes in the given tx that have been sent to a
    //! SaplingPaymentAddress in this wallet
    std::pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> FindMySaplingNotes(const CTransaction& tx) const;

    //! Find all of the addresses in the given tx that have been sent to a SaplingPaymentAddress in this wallet.
    std::vector<libzcash::SaplingPaymentAddress> FindMySaplingAddresses(const CTransaction& tx) const;

    //! Whether the nullifier is from this wallet
    bool IsSaplingNullifierFromMe(const uint256& nullifier) const;

    //! Return all of the witnesses for the input notes
    void GetSaplingNoteWitnesses(
            const std::vector<SaplingOutPoint>& notes,
            std::vector<Optional<SaplingWitness>>& witnesses,
            uint256& final_anchor);

    //! Update note data if is needed
    bool UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx);

    //! Clear every notesData from every wallet tx and reset the witness cache size
    void ClearNoteWitnessCache();

    // Sapling metadata
    std::map<libzcash::SaplingIncomingViewingKey, CKeyMetadata> mapSaplingZKeyMetadata;

    /*
     * Size of the incremental witness cache for the notes in our wallet.
     * This will always be greater than or equal to the size of the largest
     * incremental witness cache in any transaction in mapWallet.
     */
    int64_t nWitnessCacheSize{0};
    bool nWitnessCacheNeedsUpdate{false};

    /**
     * The reverse mapping of nullifiers to notes.
     *
     * The mapping cannot be updated while an encrypted wallet is locked,
     * because we need the SpendingKey to create the nullifier (zcash#1502). This has
     * several implications for transactions added to the wallet while locked:
     *
     * - Parent transactions can't be marked dirty when a child transaction that
     *   spends their output notes is updated.
     *
     *   - We currently don't cache any note values, so this is not a problem,
     *     yet.
     *
     * - GetFilteredNotes can't filter out spent notes.
     *
     *   - Per the comment in SaplingNoteData, we assume that if we don't have a
     *     cached nullifier, the note is not spent.
     *
     * Another more problematic implication is that the wallet can fail to
     * detect transactions on the blockchain that spend our notes. There are two
     * possible cases in which this could happen:
     *
     * - We receive a note when the wallet is locked, and then spend it using a
     *   different wallet client.
     *
     * - We spend from a PaymentAddress we control, then we export the
     *   SpendingKey and import it into a new wallet, and reindex/rescan to find
     *   the old transactions.
     *
     * The wallet will only miss "pure" spends - transactions that are only
     * linked to us by the fact that they contain notes we spent. If it also
     * sends notes to us, or interacts with our transparent addresses, we will
     * detect the transaction and add it to the wallet (again without caching
     * nullifiers for new notes). As by default JoinSplits send change back to
     * the origin PaymentAddress, the wallet should rarely miss transactions.
     *
     * To work around these issues, whenever the wallet is unlocked, we scan all
     * cached notes, and cache any missing nullifiers. Since the wallet must be
     * unlocked in order to spend notes, this means that GetFilteredNotes will
     * always behave correctly within that context (and any other uses will give
     * correct responses afterwards), for the transactions that the wallet was
     * able to detect. Any missing transactions can be rediscovered by:
     *
     * - Unlocking the wallet (to fill all nullifier caches).
     *
     * - Restarting the node with -reindex (which operates on a locked wallet
     *   but with the now-cached nullifiers).
     */

    std::map<uint256, SaplingOutPoint> mapSaplingNullifiersToNotes;

private:
    /* Parent wallet */
    CWallet* wallet{nullptr};
    /* the HD chain data model (external/internal chain counters) */
    CHDChain hdChain;


    /**
     * Used to keep track of spent Notes, and
     * detect and report conflicts (double-spends).
     */
    typedef std::multimap<uint256, uint256> TxNullifiers;
    TxNullifiers mapTxSaplingNullifiers;
};

#endif //PIVX_SAPLINGSCRIPTPUBKEYMAN_H
