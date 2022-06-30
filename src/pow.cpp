// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2014-2016 The BlackCoin developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"
#include "chainparams.h"
#include "tinyformat.h"



// ppcoin: find last block index up to pindex
// const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
// {
//     //CBlockIndex will be updated with information about the proof type later
//     while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
//         pindex = pindex->pprev;
//     return pindex;
// }

inline arith_uint256 GetLimit(const Consensus::Params& params, bool fProofOfStake)
{
    if(fProofOfStake) {
        return UintToArith256(params.posLimit);
    } else {
        return UintToArith256(params.powLimit);
    }
}

// unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake) {
//     /* current difficulty formula, veil - DarkGravity v3, written by Evan Duffield - evan@dash.org */
//     const arith_uint256 bnLimit = GetLimit(params, fProofOfStake);

//     const CBlockIndex *pindex = pindexLast;
//     const CBlockIndex* pindexLastMatchingProof = nullptr;
//     arith_uint256 bnPastTargetAvg = 0;
//     int nDgwPastBlocks = 30;

//     // make sure we have at least (nPastBlocks + 1) blocks, otherwise just return powLimit
//     if (!pindexLast || pindexLast->nHeight < nDgwPastBlocks)
//         return bnLimit.GetCompact();

//     unsigned int nCountBlocks = 0;
//     while (nCountBlocks < nDgwPastBlocks) {
//         // Ran out of blocks, return pow limit
//         if (!pindex)
//             return bnLimit.GetCompact();

//         // Only consider PoW or PoS blocks but not both
//         if (pindex->IsProofOfStake() != fProofOfStake) {
//             pindex = pindex->pprev;
//             continue;
//         } else if (!pindexLastMatchingProof) {
//             pindexLastMatchingProof = pindex;
//         }

//         arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
//         bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);

//         if (++nCountBlocks != nDgwPastBlocks)
//             pindex = pindex->pprev;
//     }

//     arith_uint256 bnNew(bnPastTargetAvg);

//     // Should only happen on the first PoS block
//     if (pindexLastMatchingProof)
//         pindexLastMatchingProof = pindexLast;

//     int64_t nActualTimespan = pindexLastMatchingProof->GetBlockTime() - pindex->GetBlockTime();
//     int64_t nTargetTimespan = nDgwPastBlocks * params.nTargetSpacing;

//     if (nActualTimespan < nTargetTimespan / 3)
//         nActualTimespan = nTargetTimespan / 3;

//     if (nActualTimespan > nTargetTimespan * 3)
//         nActualTimespan = nTargetTimespan * 3;

//     // Retarget
//     bnNew *= nActualTimespan;
//     bnNew /= nTargetTimespan;

//     if (bnNew > bnLimit) {
//         bnNew = bnLimit;
//     }

//     return bnNew.GetCompact();
// }






unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake) {
    int nHeight = pindexLast->nHeight + 1;

    const arith_uint256 nTargetLimit = GetLimit(params, fProofOfStake);

    int nPowTargetTimespan = params.nTargetSpacing;

    int pindexFirstShortTime = 0, pindexFirstMediumTime = 0, pindexFirstLongTime = 0;
    int shortSample = 30, mediumSample = 400, longSample = 2000;
    int nActualTimespan = 0, nActualTimespanShort = 0, nActualTimespanMedium = 0, nActualTimespanLong = 0;

    const CBlockIndex* pindexFirstLong = pindexLast;

    // i tracks sample height, j counts number of blocks of required type
    for (int i = 0, j = 0; j <= longSample + 1;) {
        bool skip = false;

        // Hit the start of the chain before finding enough blocks
        if (pindexFirstLong->pprev == nullptr)
            return nTargetLimit.GetCompact();

        // Only increment j if we have a block of the current type
        if (fProofOfStake) {
            if (pindexFirstLong->IsProofOfStake())
                j++;
            if (pindexFirstLong->pprev->IsProofOfWork())
                skip = true;
        } else {
            if (pindexFirstLong->IsProofOfWork())
                j++;
            if (pindexFirstLong->pprev->IsProofOfStake())
                skip = true;
        }

        pindexFirstLong = pindexFirstLong->pprev;

        // Do not sample on longSample - 1 due to nDiffAdjustChange bug
        if (i < longSample)
            pindexFirstLongTime = pindexFirstLong->GetBlockTime();

        if (skip) {
            continue;
        }

        if (i == shortSample - 1)
            pindexFirstShortTime = pindexFirstLong->GetBlockTime();

        if (i == mediumSample - 1)
            pindexFirstMediumTime = pindexFirstLong->GetBlockTime();

        i++;
    }

    if (pindexLast->GetBlockTime() - pindexFirstShortTime != 0)
        nActualTimespanShort = (pindexLast->GetBlockTime() - pindexFirstShortTime) / shortSample;

    if (pindexLast->GetBlockTime() - pindexFirstMediumTime != 0)
        nActualTimespanMedium = (pindexLast->GetBlockTime() - pindexFirstMediumTime) / mediumSample;

    if (pindexLast->GetBlockTime() - pindexFirstLongTime != 0)
        nActualTimespanLong = (pindexLast->GetBlockTime() - pindexFirstLongTime) / longSample;

    int nActualTimespanSum = nActualTimespanShort + nActualTimespanMedium + nActualTimespanLong;

    if (nActualTimespanSum != 0)
        nActualTimespan = nActualTimespanSum / 3;


    // Apply .25 damping
    nActualTimespan = nActualTimespan + (3 * nPowTargetTimespan);
    nActualTimespan /= 4;


    // 9% difficulty limiter
    int nActualTimespanMax = nPowTargetTimespan * 494 / 453;
    int nActualTimespanMin = nPowTargetTimespan * 453 / 494;

    if(nActualTimespan < nActualTimespanMin)
        nActualTimespan = nActualTimespanMin;

    if(nActualTimespan > nActualTimespanMax)
        nActualTimespan = nActualTimespanMax;

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= nPowTargetTimespan;

    if (bnNew <= 0 || bnNew > nTargetLimit)
        bnNew = nTargetLimit;

    return bnNew.GetCompact();
}





bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
