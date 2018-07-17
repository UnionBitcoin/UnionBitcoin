// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <limits>
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    bool is_regtest_net = false;

    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height at which BIP16 becomes active */
    int BIP16Height;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;

    /** Block height at which UAHF kicks in */
    int UBCHeight;
	int UBCInitBlockCount;
    
	/** UBC Fork to adjust block interval (ForkV1) */
	int ForkV1Height;
	
    int nStakeMinConfirmations;
    int COINBASE_MATURITY_FORKV1;
    /** Block height at which OP_RETURN replay protection stops */
    int antiReplayOpReturnSunsetHeight;
    /** Committed OP_RETURN value for replay protection */
    std::vector<uint8_t> antiReplayOpReturnCommitment;

	std::string UBCfoundationPubkey;
	std::string UBCfoundationAddress;
	std::string UBCForkGeneratorPubkey;

    int UBCONTRACT_Height;
	
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    
	/** Proof of stake parameters */
	uint256 posLimit;
	
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    void UpdateDifficultyAdjustmentIntervalForkV1() {nPowTargetTimespan = 10*1*60;nPowTargetSpacing= 1*60;nRuleChangeActivationThreshold=190;nMinerConfirmationWindow=200;}
    void UpdateDifficultyAdjustmentIntervalForkV2() {nPowTargetTimespan = 10*2*60;nPowTargetSpacing= 2*60;nRuleChangeActivationThreshold=190;nMinerConfirmationWindow=200;}

    void UpdateDifficultyAdjustmentInterval() {nPowTargetTimespan = 200*10*60;nRuleChangeActivationThreshold=190;nMinerConfirmationWindow=200;}
    void UpdateOldDifficultyAdjustmentInterval() {nPowTargetTimespan = 14*24*60*60;nRuleChangeActivationThreshold=1916;nMinerConfirmationWindow=2016;}
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
