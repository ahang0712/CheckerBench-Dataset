# Patch-overlapping Go context after the fix

## `internal/peer/gossip/mcs.go` (changed lines (154, 155, 156, 157, 266))

```go
/*
Copyright IBM Corp. All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package gossip

import (
	"bytes"
	"fmt"
	"time"

	pcommon "github.com/hyperledger/fabric-protos-go/common"
	"github.com/hyperledger/fabric/bccsp"
	"github.com/hyperledger/fabric/common/flogging"
	"github.com/hyperledger/fabric/common/policies"
	"github.com/hyperledger/fabric/common/util"
	"github.com/hyperledger/fabric/gossip/api"
	"github.com/hyperledger/fabric/gossip/common"
	"github.com/hyperledger/fabric/internal/pkg/identity"
	"github.com/hyperledger/fabric/msp"
	"github.com/hyperledger/fabric/msp/mgmt"
	"github.com/hyperledger/fabric/protoutil"
	"github.com/pkg/errors"
)

var mcsLogger = flogging.MustGetLogger("peer.gossip.mcs")

// Hasher is the interface provides the hash function should be used for all gossip components.
type Hasher interface {
	Hash(msg []byte, opts bccsp.HashOpts) (hash []byte, err error)
}

// MSPMessageCryptoService implements the MessageCryptoService interface
// using the peer MSPs (local and channel-related)
//
// In order for the system to be secure it is vital to have the
// MSPs to be up-to-date. Channels' MSPs are updated via
// configuration transactions distributed by the ordering service.
//
// A similar mechanism needs to be in place to update the local MSP, as well.
// This implementation assumes that these mechanisms are all in place and working.
type MSPMessageCryptoService struct {
	channelPolicyManagerGetter policies.ChannelPolicyManagerGetter
	localSigner                identity.SignerSerializer
	deserializer               mgmt.DeserializersManager
	hasher                     Hasher
}

// NewMCS creates a new instance of MSPMessageCryptoService
// that implements MessageCryptoService.
// The method takes in input:
// 1. a policies.ChannelPolicyManagerGetter that gives access to the policy manager of a given channel via the Manager method.
// 2. an instance of identity.SignerSerializer
// 3. an identity deserializer manager
func NewMCS(
	channelPolicyManagerGetter policies.ChannelPolicyManagerGetter,
	localSigner identity.SignerSerializer,
	deserializer mgmt.DeserializersManager,
	hasher Hasher,
) *MSPMessageCryptoService {
	return &MSPMessageCryptoService{
		channelPolicyManagerGetter: channelPolicyManagerGetter,
		localSigner:                localSigner,
		deserializer:               deserializer,
		hasher:                     hasher,
	}
}

// ValidateIdentity validates the identity of a remote peer.
// If the identity is invalid, revoked, expired it returns an error.
// Else, returns nil
func (s *MSPMessageCryptoService) ValidateIdentity(peerIdentity api.PeerIdentityType) error {
	// As prescribed by the contract of method,
	// below we check only that peerIdentity is not
	// invalid, revoked or expired.

	_, _, err := s.getValidatedIdentity(peerIdentity)
	return err
}

// GetPKIidOfCert returns the PKI-ID of a peer's identity
// If any error occurs, the method return nil
// The PKid of a peer is computed as the SHA2-256 of peerIdentity which
// is supposed to be the serialized version of MSP identity.
// This method does not validate peerIdentity.
// This validation is supposed to be done appropriately during the execution flow.
func (s *MSPMessageCryptoService) GetPKIidOfCert(peerIdentity api.PeerIdentityType) common.PKIidType {
	// Validate arguments
	if len(peerIdentity) == 0 {
		mcsLogger.Error("Invalid Peer Identity. It must be different from nil.")

		return nil
	}

	sid, err := s.deserializer.Deserialize(peerIdentity)
	if err != nil {
		mcsLogger.Errorf("Failed getting validated identity from peer identity %s: [%s]", peerIdentity, err)

		return nil
	}

	// concatenate msp-id and idbytes
	// idbytes is the low-level representation of an identity.
	// it is supposed to be already in its minimal representation

	mspIDRaw := []byte(sid.Mspid)
	raw := append(mspIDRaw, sid.IdBytes...)

	// Hash
	digest, err := s.hasher.Hash(raw, &bccsp.SHA256Opts{})
	if err != nil {
		mcsLogger.Errorf("Failed computing digest of serialized identity %s: [%s]", peerIdentity, err)
		return nil
	}

	return digest
}

// VerifyBlock returns nil if the block is properly signed, and the claimed seqNum is the
// sequence number that the block's header contains.
// else returns error
func (s *MSPMessageCryptoService) VerifyBlock(chainID common.ChannelID, seqNum uint64, block *pcommon.Block) error {
	if block.Header == nil {
		return fmt.Errorf("Invalid Block on channel [%s]. Header must be different from nil.", chainID)
	}

	blockSeqNum := block.Header.Number
	if seqNum != blockSeqNum {
		return fmt.Errorf("Claimed seqNum is [%d] but actual seqNum inside block is [%d]", seqNum, blockSeqNum)
	}

	// - Extract channelID and compare with chainID
	channelID, err := protoutil.GetChannelIDFromBlock(block)
	if err != nil {
		return fmt.Errorf("Failed getting channel id from block with id [%d] on channel [%s]: [%s]", block.Header.Number, chainID, err)
	}

	if channelID != string(chainID) {
		return fmt.Errorf("Invalid block's channel id. Expected [%s]. Given [%s]", chainID, channelID)
	}

	// - Unmarshal medatada
	if block.Metadata == nil || len(block.Metadata.Metadata) == 0 {
		return fmt.Errorf("Block with id [%d] on channel [%s] does not have metadata. Block not valid.", block.Header.Number, chainID)
	}

	metadata, err := protoutil.GetMetadataFromBlock(block, pcommon.BlockMetadataIndex_SIGNATURES)
	if err != nil {
		return fmt.Errorf("Failed unmarshalling medatata for signatures [%s]", err)
	}

	if err := protoutil.VerifyTransactionsAreWellFormed(block); err != nil {
		return fmt.Errorf("block has malformed transactions: %v", err)
	}

	// - Verify that Header.DataHash is equal to the hash of block.Data
	// This is to ensure that the header is consistent with the data carried by this block
	if !bytes.Equal(protoutil.BlockDataHash(block.Data), block.Header.DataHash) {
		return fmt.Errorf("Header.DataHash is different from Hash(block.Data) for block with id [%d] on channel [%s]", block.Header.Number, chainID)
	}

	// - Get Policy for block validation

	// Get the policy manager for channelID
	cpm := s.channelPolicyManagerGetter.Manager(channelID)
	if cpm == nil {
		return fmt.Errorf("Could not acquire policy manager for channel %s", channelID)
	}
	mcsLogger.Debugf("Got policy manager for channel [%s]", channelID)

	// Get block validation policy
	policy, ok := cpm.GetPolicy(policies.BlockValidation)
	// ok is true if it was the policy requested, or false if it is the default policy
	mcsLogger.Debugf("Got block validation policy for channel [%s] with flag [%t]", channelID, ok)

	// - Prepare SignedData
	signatureSet := []*protoutil.SignedData{}
	for _, metadataSignature := range metadata.Signatures {
		shdr, err := protoutil.UnmarshalSignatureHeader(metadataSignature.SignatureHeader)
		if err != nil {
			return fmt.Errorf("Failed unmarshalling signature header for block with id [%d] on channel [%s]: [%s]", block.Header.Number, chainID, err)
		}
		signatureSet = append(
			signatureSet,
			&protoutil.SignedData{
				Identity:  shdr.Creator,
				Data:      util.ConcatenateBytes(metadata.Value, metadataSignature.SignatureHeader, protoutil.BlockHeaderBytes(block.Header)),
				Signature: metadataSignature.Signature,
			},
		)
	}

	// - Evaluate policy
	return policy.EvaluateSignedData(signatureSet)
}

// Sign signs msg with this peer's signing key and outputs
// the signature if no error occurred.
func (s *MSPMessageCryptoService) Sign(msg []byte) ([]byte, error) {
	return s.localSigner.Sign(msg)
}

// Verify checks that signature is a valid signature of message under a peer's verification key.
// If the verification succeeded, Verify returns nil meaning no error occurred.
// If peerIdentity is nil, then the verification fails.
func (s *MSPMessageCryptoService) Verify(peerIdentity api.PeerIdentityType, signature, message []byte) error {
	identity, chainID, err := s.getValidatedIdentity(peerIdentity)
	if err != nil {
		mcsLogger.Errorf("Failed getting validated identity from peer identity [%s]", err)

		return err
	}

	if len(chainID) == 0 {
		// At this stage, this means that peerIdentity
		// belongs to this peer's LocalMSP.
		// The signature is validated directly
		return identity.Verify(message, signature)
	}

	// At this stage, the signature must be validated
	// against the reader policy of the channel
	// identified by chainID

	return s.VerifyByChannel(chainID, peerIdentity, signature, message)
}

// VerifyByChannel checks that signature is a valid signature of message
// under a peer's verification key, but also in the context of a specific channel.
// If the verification succeeded, Verify returns nil meaning no error occurred.
// If peerIdentity is nil, then the verification fails.
func (s *MSPMessageCryptoService) VerifyByChannel(chainID common.ChannelID, peerIdentity api.PeerIdentityType, signature, message []byte) error {
	// Validate arguments
	if len(peerIdentity) == 0 {
		return errors.New("Invalid Peer Identity. It must be different from nil.")
	}

	// Get the policy manager for channel chainID
	cpm := s.channelPolicyManagerGetter.Manager(string(chainID))
	if cpm == nil {
		return fmt.Errorf("Could not acquire policy manager for channel %s", string(chainID))
	}
	mcsLogger.Debugf("Got policy manager for channel [%s]", string(chainID))

	// Get channel reader policy
	policy, flag := cpm.GetPolicy(policies.ChannelApplicationReaders)
	mcsLogger.Debugf("Got reader policy for channel [%s] with flag [%t]", string(chainID), flag)

	return policy.EvaluateSignedData(
		[]*protoutil.SignedData{{
			Data:      message,
			Identity:  []byte(peerIdentity),
			Signature: signature,
		}},
	)
}

func (s *MSPMessageCryptoService) Expiration(peerIdentity api.PeerIdentityType) (time.Time, error) {
	id, _, err := s.getValidatedIdentity(peerIdentity)
	if err != nil {
		return time.Time{}, errors.Wrap(err, "Unable to extract msp.Identity from peer Identity")
	}
	return id.ExpiresAt(), nil
}

func (s *MSPMessageCryptoService) getValidatedIdentity(peerIdentity api.PeerIdentityType) (msp.Identity, common.ChannelID, error) {
	// Validate arguments
	if len(peerIdentity) == 0 {
		return nil, nil, errors.New("Invalid Peer Identity. It must be different from nil.")
	}

	sId, err := s.deserializer.Deserialize(peerIdentity)
	if err != nil {
		mcsLogger.Error("failed deserializing identity", err)
		return nil, nil, err
	}

	// Notice that peerIdentity is assumed to be the serialization of an identity.
	// So, first step is the identity deserialization and then verify it.

	// First check against the local MSP.
	// If the peerIdentity is in the same organization of this node then
	// the local MSP is required to take the final decision on the validity
	// of the signature.
	lDes := s.deserializer.GetLocalDeserializer()
	identity, err := lDes.DeserializeIdentity([]byte(peerIdentity))
	if err == nil {
		// No error means that the local MSP successfully deserialized the identity.
		// We now check additional properties.
		if err := lDes.IsWellFormed(sId); err != nil {
			return nil, nil, errors.Wrap(err, "identity is not well formed")
		}
		// TODO: The following check will be replaced by a check on the organizational units
		// when we allow the gossip network to have organization unit (MSP subdivisions)
		// scoped messages.
		// The following check is consistent with the SecurityAdvisor#OrgByPeerIdentity
		// implementation.
		// TODO: Notice that the following check saves us from the fact
		// that DeserializeIdentity does not yet enforce MSP-IDs consistency.
		// This check can be removed once DeserializeIdentity will be fixed.
		if identity.GetMSPIdentifier() == s.deserializer.GetLocalMSPIdentifier() {
			// Check identity validity

			// Notice that at this stage we don't have to check the identity
			// against any channel's policies.
			// This will be done by the caller function, if needed.
			return identity, nil, identity.Validate()
		}
	}

	// Check against managers
	for chainID, mspManager := range s.deserializer.GetChannelDeserializers() {
		// Deserialize identity
		identity, err := mspManager.DeserializeIdentity([]byte(peerIdentity))
		if err != nil {
			mcsLogger.Debugf("Failed deserialization identity %s on [%s]: [%s]", peerIdentity, chainID, err)
			continue
		}

		// We managed deserializing the identity with this MSP manager. Now we check if it's well formed.
		if err := mspManager.IsWellFormed(sId); err != nil {
			return nil, nil, errors.Wrap(err, "identity is not well formed")
		}

		// Check identity validity
		// Notice that at this stage we don't have to check the identity
		// against any channel's policies.
		// This will be done by the caller function, if needed.

		if err := identity.Validate(); err != nil {
			mcsLogger.Debugf("Failed validating identity %s on [%s]: [%s]", peerIdentity, chainID, err)
			continue
		}

		mcsLogger.Debugf("Validation succeeded %s on [%s]", peerIdentity, chainID)

		return identity, common.ChannelID(chainID), nil
	}

	return nil, nil, fmt.Errorf("Peer Identity %s cannot be validated. No MSP found able to do that.", peerIdentity)
}
```

## `orderer/common/cluster/replication_test.go` (changed lines (133, 142, 143, 324, 445, 446, 447, 448))

```go
/*
Copyright IBM Corp. All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package cluster_test

import (
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/hyperledger/fabric-protos-go/common"
	"github.com/hyperledger/fabric-protos-go/msp"
	"github.com/hyperledger/fabric-protos-go/orderer"
	"github.com/hyperledger/fabric/bccsp/sw"
	"github.com/hyperledger/fabric/common/channelconfig"
	"github.com/hyperledger/fabric/common/configtx"
	"github.com/hyperledger/fabric/common/flogging"
	"github.com/hyperledger/fabric/internal/pkg/comm"
	"github.com/hyperledger/fabric/orderer/common/cluster"
	"github.com/hyperledger/fabric/orderer/common/cluster/mocks"
	"github.com/hyperledger/fabric/orderer/common/localconfig"
	"github.com/hyperledger/fabric/protoutil"
	"github.com/pkg/errors"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

func TestIsReplicationNeeded(t *testing.T) {
	for _, testCase := range []struct {
		name                string
		bootBlock           *common.Block
		systemChannelHeight uint64
		systemChannelError  error
		expectedError       string
		replicationNeeded   bool
	}{
		{
			name:                "no replication needed",
			systemChannelHeight: 100,
			bootBlock:           &common.Block{Header: &common.BlockHeader{Number: 99}},
		},
		{
			name:                "replication is needed - bootstrap block's index equal to height",
			systemChannelHeight: 99,
			bootBlock:           &common.Block{Header: &common.BlockHeader{Number: 99}},
			replicationNeeded:   true,
		},
		{
			name:                "replication is needed - no ledger",
			systemChannelHeight: 0,
			bootBlock:           &common.Block{Header: &common.BlockHeader{Number: 99}},
			replicationNeeded:   true,
		},
		{
			name:               "IO error",
			systemChannelError: errors.New("IO error"),
			expectedError:      "IO error",
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			ledgerWriter := &mocks.LedgerWriter{}
			ledgerWriter.On("Height").Return(testCase.systemChannelHeight)

			ledgerFactory := &mocks.LedgerFactory{}
			ledgerFactory.On("GetOrCreate", "system").Return(ledgerWriter, testCase.systemChannelError)

			r := cluster.Replicator{
				Filter:        cluster.AnyChannel,
				Logger:        flogging.MustGetLogger("test"),
				BootBlock:     testCase.bootBlock,
				SystemChannel: "system",
				LedgerFactory: ledgerFactory,
			}

			ok, err := r.IsReplicationNeeded()
			if testCase.expectedError != "" {
				assert.EqualError(t, err, testCase.expectedError)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, testCase.replicationNeeded, ok)
			}
		})
	}
}

func TestReplicateChainsFailures(t *testing.T) {
	for _, testCase := range []struct {
		name                    string
		isProbeResponseDelayed  bool
		latestBlockSeqInOrderer uint64
		ledgerFactoryError      error
		appendBlockError        error
		expectedPanic           string
		mutateBlocks            func([]*common.Block)
		channelsReturns         []cluster.ChannelGenesisBlock
		badResponse             *orderer.DeliverResponse
	}{
		{
			name: "no block received",
			expectedPanic: "Failed pulling system channel: " +
				"failed obtaining the latest block for channel system",
			isProbeResponseDelayed: true,
		},
		{
			name: "received service unavailable",
			expectedPanic: "Failed pulling system channel: " +
				"failed obtaining the latest block for channel system",
			badResponse: &orderer.DeliverResponse{
				Type: &orderer.DeliverResponse_Status{
					Status: common.Status_SERVICE_UNAVAILABLE,
				},
			},
		},
		{
			name: "latest block seq is less than boot block seq",
			expectedPanic: "Failed pulling system channel: " +
				"latest height found among system channel(system) orderers is 19," +
				" but the boot block's sequence is 21",
			latestBlockSeqInOrderer: 18,
		},
		{
			name: "hash chain mismatch",
			expectedPanic: "Failed pulling system channel: " +
				"block header mismatch on sequence 11, " +
				"expected 229de8d87db1ddf7278093bc65ba9245cd3acd8ccfc687e0edc68adf9b181488, got 010203",
			latestBlockSeqInOrderer: 21,
			mutateBlocks: func(systemChannelBlocks []*common.Block) {
				systemChannelBlocks[len(systemChannelBlocks)/2].Header.PreviousHash = []byte{1, 2, 3}
			},
		},
		{
			name: "last pulled block doesn't match the boot block",
			expectedPanic: "Block header mismatch on last system channel block," +
				" expected 0bea18bff7feeaa0bc08f528e1f563c818fc0633e291786c96eedffd8c7e6cff," +
				" got 924c568c4a4e8f16e3cbd123ea0ae38d0bbb01d60bafb74f4bb55f108c0eb194",
			latestBlockSeqInOrderer: 21,
			mutateBlocks: func(systemChannelBlocks []*common.Block) {
				systemChannelBlocks[21].Header.DataHash = nil
			},
		},
		{
			name:                    "failure in creating ledger",
			latestBlockSeqInOrderer: 21,
			ledgerFactoryError:      errors.New("IO error"),
			expectedPanic:           "Failed to create a ledger for channel system: IO error",
		},
		{
			name:                    "failure in appending a block to the ledger",
			latestBlockSeqInOrderer: 21,
			appendBlockError:        errors.New("IO error"),
			expectedPanic:           "Failed to write block [0]: IO error",
		},
		{
			name:                    "failure pulling the system chain",
			latestBlockSeqInOrderer: 21,
			expectedPanic: "Failed pulling system channel: " +
				"failed obtaining the latest block for channel system",
			isProbeResponseDelayed: true,
		},
		{
			name:                    "failure obtaining a ledger for a non participating channel",
			latestBlockSeqInOrderer: 21,
			channelsReturns: []cluster.ChannelGenesisBlock{
				{ChannelName: "channelWeAreNotPartOf"},
			},
			ledgerFactoryError: errors.New("IO error"),
			expectedPanic:      "Failed to create a ledger for channel channelWeAreNotPartOf: IO error",
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			systemChannelBlocks := createBlockChain(0, 21)
			if testCase.mutateBlocks != nil {
				testCase.mutateBlocks(systemChannelBlocks)
			}

			lw := &mocks.LedgerWriter{}
			lw.On("Append", mock.Anything).Return(testCase.appendBlockError)
			lw.On("Height").Return(uint64(0))

			lf := &mocks.LedgerFactory{}
			lf.On("GetOrCreate", "system").Return(lw, testCase.ledgerFactoryError)
			lf.On("GetOrCreate", "channelWeAreNotPartOf").Return(lw, testCase.ledgerFactoryError)

			osn := newClusterNode(t)
			defer osn.stop()

			dialer := newCountingDialer()
			bp := newBlockPuller(dialer, osn.srv.Address())
			// Put a big timeout, to reduce chance of flakes when the server gets stuck
			// and we get an un-called for timeout.
			bp.FetchTimeout = time.Hour

			cl := &mocks.ChannelLister{}
			cl.On("Channels").Return(testCase.channelsReturns)
			cl.On("Close")

			r := cluster.Replicator{
				Filter: cluster.AnyChannel,
				AmIPartOfChannel: func(configBlock *common.Block) error {
					return cluster.ErrNotInChannel
				},
				Logger:        flogging.MustGetLogger("test"),
				BootBlock:     systemChannelBlocks[21],
				SystemChannel: "system",
				LedgerFactory: lf,
				Puller:        bp,
				ChannelLister: cl,
			}

			if len(testCase.channelsReturns) > 0 {
				simulateNonParticipantChannelPull(osn)
			}

			if testCase.badResponse != nil {
				osn.blockResponses <- testCase.badResponse
			}

			if !testCase.isProbeResponseDelayed {
				osn.enqueueResponse(testCase.latestBlockSeqInOrderer)
				osn.enqueueResponse(testCase.latestBlockSeqInOrderer)
			} else {
				// Send a nil to force an EOF to the client
				osn.blockResponses <- nil
			}
			osn.addExpectProbeAssert()
			osn.addExpectProbeAssert()
			osn.addExpectPullAssert(0)

			if !testCase.isProbeResponseDelayed {
				for _, block := range systemChannelBlocks {
					osn.blockResponses <- &orderer.DeliverResponse{
						Type: &orderer.DeliverResponse_Block{Block: block},
					}
				}
			} else {
				// Send a nil to force an EOF to the client
				osn.blockResponses <- nil
			}

			assert.PanicsWithValue(t, testCase.expectedPanic, func() { r.ReplicateChains() })
			bp.Close()
			dialer.assertAllConnectionsClosed(t)
		})
	}
}

func TestPullChannelFailure(t *testing.T) {
	blockchain := createBlockChain(0, 5)
	for _, testcase := range []struct {
		name                 string
		genesisBlockSequence int
		thirdBlockSequence   int
	}{
		{
			name:                 "Failed to pull genesis block",
			genesisBlockSequence: 1,
		},
		{
			name:                 "Failed to pull some non genesis block",
			genesisBlockSequence: 0,
			thirdBlockSequence:   0,
		},
	} {
		t.Run(testcase.name, func(t *testing.T) {
			lw := &mocks.LedgerWriter{}
			lw.On("Append", mock.Anything).Return(nil)
			lw.On("Height").Return(uint64(0))

			lf := &mocks.LedgerFactory{}
			lf.On("GetOrCreate", "mychannel").Return(lw, nil)

			osn := newClusterNode(t)
			defer osn.stop()

			enqueueBlock := func(seq int) {
				osn.blockResponses <- &orderer.DeliverResponse{
					Type: &orderer.DeliverResponse_Block{
						Block: blockchain[seq],
					},
				}
			}

			dialer := newCountingDialer()
			bp := newBlockPuller(dialer, osn.srv.Address())
			// Put a big timeout, to reduce chance of flakes when the server gets stuck
			// and we get an un-called for timeout.
			bp.FetchTimeout = time.Hour
			bp.MaxPullBlockRetries = 1
			// Do not buffer blocks in memory
			bp.MaxTotalBufferBytes = 1

			r := cluster.Replicator{
				Filter: cluster.AnyChannel,
				AmIPartOfChannel: func(configBlock *common.Block) error {
					return nil
				},
				Logger:        flogging.MustGetLogger("test"),
				SystemChannel: "system",
				LedgerFactory: lf,
				Puller:        bp,
			}

			osn.addExpectProbeAssert()
			enqueueBlock(5)
			osn.addExpectProbeAssert()
			enqueueBlock(5)
			osn.addExpectPullAssert(0)
			enqueueBlock(testcase.genesisBlockSequence)
			enqueueBlock(1)
			enqueueBlock(testcase.thirdBlockSequence)

			err := r.PullChannel("mychannel")
			assert.Equal(t, cluster.ErrRetryCountExhausted, err)
		})
	}
}

func TestPullerConfigFromTopLevelConfig(t *testing.T) {
	signer := &mocks.SignerSerializer{}
	expected := cluster.PullerConfig{
		Channel:             "system",
		MaxTotalBufferBytes: 100,
		Signer:              signer,
		TLSCert:             []byte{3, 2, 1},
		TLSKey:              []byte{1, 2, 3},
		Timeout:             time.Hour,
	}

	topLevelConfig := &localconfig.TopLevel{
		General: localconfig.General{
			Cluster: localconfig.Cluster{
				ReplicationBufferSize: 100,
				RPCTimeout:            time.Hour,
			},
		},
	}

	config := cluster.PullerConfigFromTopLevelConfig("system", topLevelConfig, []byte{1, 2, 3}, []byte{3, 2, 1}, signer)
	assert.Equal(t, expected, config)
}

func TestReplicateChainsChannelClassificationFailure(t *testing.T) {
	// Scenario: We are unable to classify whether we are part of the channel,
	// so we crash, because this is a programming error.

	block30WithConfigBlockOf21 := protoutil.NewBlock(30, nil)
	block30WithConfigBlockOf21.Metadata.Metadata[common.BlockMetadataIndex_LAST_CONFIG] = protoutil.MarshalOrPanic(&common.Metadata{
		Value: protoutil.MarshalOrPanic(&common.LastConfig{Index: 21}),
	})

	osn := newClusterNode(t)
	defer osn.stop()
	osn.blockResponses = make(chan *orderer.DeliverResponse, 1000)

	dialer := newCountingDialer()
	bp := newBlockPuller(dialer, osn.srv.Address())
	bp.FetchTimeout = time.Hour

	channelLister := &mocks.ChannelLister{}
	channelLister.On("Channels").Return([]cluster.ChannelGenesisBlock{{ChannelName: "A"}})
	channelLister.On("Close")

	// We probe for the latest block of the orderer
	osn.addExpectProbeAssert()
	osn.enqueueResponse(30)

	// And now pull it again (first poll and then pull it for real).
	osn.addExpectProbeAssert()
	osn.enqueueResponse(30)
	osn.addExpectPullAssert(30)
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: block30WithConfigBlockOf21},
	}
	// Now we pull the latest config block extracted from the previous block pulled.
	// Beforehand we reconnect to the orderer, so we put an artificial signal to close the stream on the server side,
	// in order to expect for a new stream to be established.
	osn.blockResponses <- nil
	// The orderer's last block's sequence is 30,
	osn.addExpectProbeAssert()
	osn.enqueueResponse(30)
	// And the Replicator now asks for block 21.
	osn.enqueueResponse(21)
	osn.addExpectPullAssert(21)

	r := cluster.Replicator{
		Filter: cluster.AnyChannel,
		AmIPartOfChannel: func(configBlock *common.Block) error {
			return errors.New("oops")
		},
		Logger:        flogging.MustGetLogger("test"),
		SystemChannel: "system",
		ChannelLister: channelLister,
		Puller:        bp,
	}

	assert.PanicsWithValue(t, "Failed classifying whether I belong to channel A: oops, skipping chain retrieval", func() {
		r.ReplicateChains()
	})

	bp.Close()
	dialer.assertAllConnectionsClosed(t)
}

func TestReplicateChainsGreenPath(t *testing.T) {
	// Scenario: There are 5 channels in the system: A-E.
	// We are in channel A but not in channel B, therefore
	// we should pull channel A and then the system channel.
	// However, this is not the first attempt of replication for
	// our node, but the second.
	// In the past, the node pulled 10 blocks of channel A and crashed.
	// Therefore, it should pull blocks, but commit for channel A
	// only blocks starting from block number 10.
	// For channel C - we are forbidden from pulling any blocks.
	// Channel D is a deserted channel - all OSNs have left it,
	// therefore we should not pull it at all.
	// Channel E cannot be pulled at all, due to the OSN being unavailable
	// at that time.

	systemChannelBlocks := createBlockChain(0, 21)
	block30WithConfigBlockOf21 := protoutil.NewBlock(30, nil)
	block30WithConfigBlockOf21.Metadata.Metadata[common.BlockMetadataIndex_LAST_CONFIG] = protoutil.MarshalOrPanic(&common.Metadata{
		Value: protoutil.MarshalOrPanic(&common.LastConfig{Index: 21}),
	})

	osn := newClusterNode(t)
	defer osn.stop()
	osn.blockResponses = make(chan *orderer.DeliverResponse, 1000)

	dialer := newCountingDialer()
	bp := newBlockPuller(dialer, osn.srv.Address())
	bp.FetchTimeout = time.Hour
	bp.MaxPullBlockRetries = 1

	channelLister := &mocks.ChannelLister{}
	channelLister.On("Channels").Return([]cluster.ChannelGenesisBlock{
		{ChannelName: "E", GenesisBlock: fakeGB},
		{ChannelName: "D", GenesisBlock: fakeGB},
		{ChannelName: "C", GenesisBlock: fakeGB},
		{ChannelName: "A", GenesisBlock: fakeGB},
		{ChannelName: "B", GenesisBlock: fakeGB},
	})
	channelLister.On("Close")

	amIPartOfChannelMock := &mock.Mock{}
	// For channel A
	amIPartOfChannelMock.On("func13").Return(nil).Once()
	// For channel B
	amIPartOfChannelMock.On("func13").Return(cluster.ErrNotInChannel).Once()

	// 22 is for the system channel, and 31 is for channel A, and for channel B we only need 1 block (the GB).
	blocksCommittedToLedgerA := make(chan *common.Block, 31)
	blocksCommittedToLedgerB := make(chan *common.Block, 1)
	blocksCommittedToLedgerC := make(chan *common.Block, 1)
	blocksCommittedToLedgerD := make(chan *common.Block, 1)
	blocksCommittedToLedgerE := make(chan *common.Block, 1)
	blocksCommittedToSystemLedger := make(chan *common.Block, 22)
	// Put 10 blocks in the ledger of channel A, to simulate
	// that the ledger had blocks when the node started.
	for seq := 0; seq < 10; seq++ {
		blocksCommittedToLedgerA <- &common.Block{
			Header: &common.BlockHeader{Number: uint64(seq)},
		}
	}

	lwA := &mocks.LedgerWriter{}
	lwA.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToLedgerA <- arg.Get(0).(*common.Block)
	})
	lwA.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToLedgerA))
	})

	lwB := &mocks.LedgerWriter{}
	lwB.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToLedgerB))
	})
	lwB.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToLedgerB <- arg.Get(0).(*common.Block)
	})

	lwC := &mocks.LedgerWriter{}
	lwC.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToLedgerC))
	})
	lwC.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToLedgerC <- arg.Get(0).(*common.Block)
	})

	lwD := &mocks.LedgerWriter{}
	lwD.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToLedgerD))
	})
	lwD.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToLedgerD <- arg.Get(0).(*common.Block)
	})

	lwE := &mocks.LedgerWriter{}
	lwE.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToLedgerE))
	})
	lwE.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToLedgerE <- arg.Get(0).(*common.Block)
	})

	lwSystem := &mocks.LedgerWriter{}
	lwSystem.On("Append", mock.Anything).Return(nil).Run(func(arg mock.Arguments) {
		blocksCommittedToSystemLedger <- arg.Get(0).(*common.Block)
	})
	lwSystem.On("Height").Return(func() uint64 {
		return uint64(len(blocksCommittedToSystemLedger))
	})

	lf := &mocks.LedgerFactory{}
	lf.On("Close")
	lf.On("GetOrCreate", "A").Return(lwA, nil)
	lf.On("GetOrCreate", "B").Return(lwB, nil)
	lf.On("GetOrCreate", "C").Return(lwC, nil)
	lf.On("GetOrCreate", "D").Return(lwD, nil)
	lf.On("GetOrCreate", "E").Return(lwE, nil)
	lf.On("GetOrCreate", "system").Return(lwSystem, nil)

	r := cluster.Replicator{
		Filter:        cluster.AnyChannel,
		LedgerFactory: lf,
		AmIPartOfChannel: func(configBlock *common.Block) error {
			return amIPartOfChannelMock.Called().Error(0)
		},
		Logger:        flogging.MustGetLogger("test"),
		SystemChannel: "system",
		ChannelLister: channelLister,
		Puller:        bp,
		BootBlock:     systemChannelBlocks[21],
	}

	// The first thing the orderer gets is a seek to channel E.
	// Unfortunately, it's not available!
	osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
		// Ensure the seek came to the right channel
		assert.NotNil(osn.t, info.GetStart().GetNewest())
		assert.Equal(t, "E", actualChannel)
	}
	// Send an EOF down the stream.
	osn.blockResponses <- nil

	// The second thing the orderer gets is a seek to channel D,
	// which is followed by a response of service unavailable
	osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
		// Ensure the seek came to the right channel
		assert.NotNil(osn.t, info.GetStart().GetNewest())
		assert.Equal(t, "D", actualChannel)
	}
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Status{
			Status: common.Status_SERVICE_UNAVAILABLE,
		},
	}

	// The third thing the orderer gets is a seek to channel C,
	// which is followed by a response of forbidden
	osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
		// Ensure the seek came to the right channel
		assert.NotNil(osn.t, info.GetStart().GetNewest())
		assert.Equal(t, "C", actualChannel)
	}

	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Status{
			Status: common.Status_FORBIDDEN,
		},
	}

	for _, channel := range []string{"A", "B"} {
		channel := channel
		// First, the orderer needs to figure out whether it is in the channel,
		// so it reaches to find the latest block from all orderers to get
		// the latest config block and see whether it is among the consenters.

		// Orderer is expecting a poll for last block of the current channel
		osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
			// Ensure the seek came to the right channel
			assert.NotNil(osn.t, info.GetStart().GetNewest())
			assert.Equal(t, channel, actualChannel)
		}

		// Orderer returns its last block is 30.
		// This is needed to get the latest height by comparing among all orderers.
		osn.enqueueResponse(30)

		// First we poll for the block sequence we got previously again, from some orderer.
		osn.addExpectProbeAssert()
		osn.enqueueResponse(30)

		// And afterwards pull the block from the first orderer.
		osn.addExpectPullAssert(30)
		osn.blockResponses <- &orderer.DeliverResponse{
			Type: &orderer.DeliverResponse_Block{Block: block30WithConfigBlockOf21},
		}
		// And the last config block is pulled via reconnecting to the orderer.
		osn.blockResponses <- nil
		// The orderer's last block's sequence is 30,
		osn.addExpectProbeAssert()
		osn.enqueueResponse(30)
		// And the Replicator now asks for block 21.
		osn.enqueueResponse(21)
		osn.addExpectPullAssert(21)
		// We always close the connection before attempting to pull the next block
		osn.blockResponses <- nil
	}

	// Next, the Replicator figures out the latest block sequence for that chain
	// to know until when to pull

	// We expect a probe for channel A only, because channel B isn't in the channel
	osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
		// Ensure the seek came to the right channel
		assert.NotNil(osn.t, info.GetStart().GetNewest())
		assert.Equal(t, "A", actualChannel)
	}
	osn.enqueueResponse(30)
	// From this point onwards, we pull the blocks for the chain.
	osn.enqueueResponse(30)
	osn.addExpectProbeAssert()
	osn.addExpectPullAssert(10)
	// Enqueue 31 blocks in its belly
	for _, block := range createBlockChain(10, 30) {
		osn.blockResponses <- &orderer.DeliverResponse{
			Type: &orderer.DeliverResponse_Block{Block: block},
		}
	}
	// Signal the orderer to stop sending us blocks since we're going to reconnect
	// to it to ask for the next channel
	osn.blockResponses <- nil

	// Now we define assertions for the system channel
	// Pull assertions for the system channel
	osn.seekAssertions <- func(info *orderer.SeekInfo, actualChannel string) {
		// Ensure the seek came to the system channel.
		assert.NotNil(osn.t, info.GetStart().GetNewest())
		assert.Equal(t, "system", actualChannel)
	}
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: systemChannelBlocks[21]},
	}
	osn.addExpectProbeAssert()
	osn.enqueueResponse(21)
	osn.addExpectPullAssert(0)
	for _, block := range systemChannelBlocks {
		osn.blockResponses <- &orderer.DeliverResponse{
			Type: &orderer.DeliverResponse_Block{Block: block},
		}
	}

	// This is where all the work is done.
	// The above lines were all assertions and preparations
	// for the expected flow of the test.
	r.ReplicateChains()

	// We replicated the chains, so all that left is to ensure
	// the blocks were committed in order, and all blocks we expected
	// to be committed (for channel A and the system channel) were committed.
	close(blocksCommittedToLedgerA)
	close(blocksCommittedToSystemLedger)
	assert.Len(t, blocksCommittedToLedgerA, cap(blocksCommittedToLedgerA))
	assert.Len(t, blocksCommittedToSystemLedger, cap(blocksCommittedToSystemLedger))
	assert.Len(t, blocksCommittedToLedgerB, 1)
	assert.Len(t, blocksCommittedToLedgerC, 1)
	assert.Len(t, blocksCommittedToLedgerD, 1)
	assert.Len(t, blocksCommittedToLedgerE, 1)
	// Count the blocks for channel A
	var expectedSequence uint64
	for block := range blocksCommittedToLedgerA {
		assert.Equal(t, expectedSequence, block.Header.Number)
		expectedSequence++
	}

	// Count the blocks for the system channel
	expectedSequence = uint64(0)
	for block := range blocksCommittedToSystemLedger {
		assert.Equal(t, expectedSequence, block.Header.Number)
		expectedSequence++
	}

	bp.Close()
	dialer.assertAllConnectionsClosed(t)
}

func TestParticipant(t *testing.T) {
	for _, testCase := range []struct {
		name                  string
		heightsByEndpoints    map[string]uint64
		heightsByEndpointsErr error
		latestBlockSeq        uint64
		latestBlock           *common.Block
		latestConfigBlockSeq  uint64
		latestConfigBlock     *common.Block
		expectedError         string
		predicateReturns      error
	}{
		{
			name:          "No available orderer",
			expectedError: cluster.ErrRetryCountExhausted.Error(),
		},
		{
			name:                  "Unauthorized for the channel",
			expectedError:         cluster.ErrForbidden.Error(),
			heightsByEndpointsErr: cluster.ErrForbidden,
		},
		{
			name:                  "No OSN services the channel",
			expectedError:         cluster.ErrServiceUnavailable.Error(),
			heightsByEndpointsErr: cluster.ErrServiceUnavailable,
		},
		{
			name: "Pulled block has no metadata",
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock:    &common.Block{},
			expectedError:  "failed to retrieve metadata: no metadata in block",
		},
		{
			name: "Pulled block has no last config sequence in metadata",
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: nil,
				},
			},
			expectedError: "failed to retrieve metadata: no metadata at index [SIGNATURES]",
		},
		{
			name: "Pulled block's SIGNATURES metadata is malformed",
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{{1, 2, 3}},
				},
			},
			expectedError: "failed to retrieve metadata: error unmarshaling metadata" +
				" at index [SIGNATURES]: proto: common.Metadata: illegal tag 0 (wire type 1)",
		},
		{
			name: "Pulled block's LAST_CONFIG metadata is malformed",
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{{}, {1, 2, 3}},
				},
			},
			expectedError: "failed to retrieve metadata: error unmarshaling metadata" +
				" at index [LAST_CONFIG]: proto: common.Metadata: illegal tag 0 (wire type 1)",
		},
		{
			name: "Pulled block's metadata is valid and has a last config",
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{protoutil.MarshalOrPanic(&common.Metadata{
						Value: protoutil.MarshalOrPanic(&common.OrdererBlockMetadata{
							LastConfig: &common.LastConfig{Index: 42},
						}),
					})},
				},
			},
			latestConfigBlockSeq: 42,
			latestConfigBlock:    &common.Block{Header: &common.BlockHeader{Number: 42}},
			predicateReturns:     cluster.ErrNotInChannel,
		},
		{
			name:          "Failed pulling last block",
			expectedError: cluster.ErrRetryCountExhausted.Error(),
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock:    nil,
		},
		{
			name:          "Failed pulling last config block",
			expectedError: cluster.ErrRetryCountExhausted.Error(),
			heightsByEndpoints: map[string]uint64{
				"orderer.example.com:7050": 100,
			},
			latestBlockSeq: uint64(99),
			latestBlock: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{protoutil.MarshalOrPanic(&common.Metadata{
						Value: protoutil.MarshalOrPanic(&common.OrdererBlockMetadata{
							LastConfig: &common.LastConfig{Index: 42},
						}),
					})},
				},
			},
			latestConfigBlockSeq: 42,
			latestConfigBlock:    nil,
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			configBlocks := make(chan *common.Block, 1)
			predicate := func(configBlock *common.Block) error {
				configBlocks <- configBlock
				return testCase.predicateReturns
			}
			puller := &mocks.ChainPuller{}
			puller.On("HeightsByEndpoints").Return(testCase.heightsByEndpoints, testCase.heightsByEndpointsErr)
			puller.On("PullBlock", testCase.latestBlockSeq).Return(testCase.latestBlock)
			puller.On("PullBlock", testCase.latestConfigBlockSeq).Return(testCase.latestConfigBlock)
			puller.On("Close")

			err := cluster.Participant(puller, predicate)
			if testCase.expectedError != "" {
				assert.EqualError(t, err, testCase.expectedError)
				assert.Len(t, configBlocks, 0)
			} else {
				assert.Len(t, configBlocks, 1)
				assert.Equal(t, testCase.predicateReturns, err)
			}
		})
	}
}

func TestBlockPullerFromConfigBlockFailures(t *testing.T) {
	blockBytes, err := ioutil.ReadFile("testdata/mychannel.block")
	assert.NoError(t, err)

	validBlock := &common.Block{}
	assert.NoError(t, proto.Unmarshal(blockBytes, validBlock))

	cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
	assert.NoError(t, err)

	for _, testCase := range []struct {
		name         string
		expectedErr  string
		pullerConfig cluster.PullerConfig
		block        *common.Block
	}{
		{
			name:        "nil block",
			expectedErr: "nil block",
		},
		{
			name:        "invalid block",
			expectedErr: "block data is nil",
			block:       &common.Block{},
		},
		{
			name: "bad envelope inside block",
			expectedErr: "failed extracting bundle from envelope: " +
				"failed to unmarshal payload from envelope: " +
				"error unmarshaling Payload: " +
				"proto: common.Payload: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: []byte{1, 2, 3},
					})},
				},
			},
		},
		{
			name:        "invalid TLS certificate",
			expectedErr: "unable to decode TLS certificate PEM: ////",
			block:       validBlock,
			pullerConfig: cluster.PullerConfig{
				TLSCert: []byte{255, 255, 255},
			},
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			verifierRetriever := &mocks.VerifierRetriever{}
			verifierRetriever.On("RetrieveVerifier", mock.Anything).Return(&cluster.NoopBlockVerifier{})
			bp, err := cluster.BlockPullerFromConfigBlock(testCase.pullerConfig, testCase.block, verifierRetriever, cryptoProvider)
			assert.EqualError(t, err, testCase.expectedErr)
			assert.Nil(t, bp)
		})
	}
}

func testBlockPullerFromConfig(t *testing.T, blockVerifiers []cluster.BlockVerifier, expectedLogMsg string, iterations int) {
	verifierRetriever := &mocks.VerifierRetriever{}
	for _, blockVerifier := range blockVerifiers {
		verifierRetriever.On("RetrieveVerifier", mock.Anything).Return(blockVerifier).Once()
	}

	caCert, err := ioutil.ReadFile(filepath.Join("testdata", "ca.crt"))
	assert.NoError(t, err)

	tlsCert, err := ioutil.ReadFile(filepath.Join("testdata", "server.crt"))
	assert.NoError(t, err)

	tlsKey, err := ioutil.ReadFile(filepath.Join("testdata", "server.key"))
	assert.NoError(t, err)

	osn := newClusterNode(t)
	osn.srv.Stop()
	// Replace the gRPC server with a TLS one
	osn.srv, err = comm.NewGRPCServer("127.0.0.1:0", comm.ServerConfig{
		SecOpts: comm.SecureOptions{
			Key:               tlsKey,
			RequireClientCert: true,
			Certificate:       tlsCert,
			ClientRootCAs:     [][]byte{caCert},
			UseTLS:            true,
		},
	})
	assert.NoError(t, err)
	orderer.RegisterAtomicBroadcastServer(osn.srv.Server(), osn)
	// And start it
	go osn.srv.Start()
	defer osn.stop()

	// Start from a valid configuration block
	blockBytes, err := ioutil.ReadFile(filepath.Join("testdata", "mychannel.block"))
	assert.NoError(t, err)

	validBlock := &common.Block{}
	assert.NoError(t, proto.Unmarshal(blockBytes, validBlock))

	// And inject into it a 127.0.0.1 orderer endpoint endpoint and a new TLS CA certificate.
	injectTLSCACert(t, validBlock, caCert)
	injectGlobalOrdererEndpoint(t, validBlock, osn.srv.Address())
	validBlock.Header.DataHash = protoutil.BlockDataHash(validBlock.Data)

	for attempt := 0; attempt < iterations; attempt++ {
		blockMsg := &orderer.DeliverResponse_Block{
			Block: validBlock,
		}

		osn.blockResponses <- &orderer.DeliverResponse{
			Type: blockMsg,
		}

		osn.blockResponses <- &orderer.DeliverResponse{
			Type: blockMsg,
		}

		osn.blockResponses <- nil

		osn.addExpectProbeAssert()
		osn.addExpectPullAssert(0)
	}

	cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
	assert.NoError(t, err)

	bp, err := cluster.BlockPullerFromConfigBlock(cluster.PullerConfig{
		TLSCert:             tlsCert,
		TLSKey:              tlsKey,
		MaxTotalBufferBytes: 1,
		Channel:             "mychannel",
		Signer:              &mocks.SignerSerializer{},
		Timeout:             time.Hour,
	}, validBlock, verifierRetriever, cryptoProvider)
	bp.RetryTimeout = time.Millisecond * 10
	assert.NoError(t, err)
	defer bp.Close()

	var seenExpectedLogMsg bool

	bp.Logger = bp.Logger.WithOptions(zap.Hooks(func(entry zapcore.Entry) error {
		if strings.Contains(entry.Message, expectedLogMsg) {
			seenExpectedLogMsg = true
		}
		return nil
	}))

	block := bp.PullBlock(0)
	assert.Equal(t, uint64(0), block.Header.Number)
	assert.True(t, seenExpectedLogMsg)
}

func TestSkipPullingPulledChannels(t *testing.T) {
	blockchain := createBlockChain(0, 5)
	lw := &mocks.LedgerWriter{}
	lw.On("Height").Return(uint64(6))

	lf := &mocks.LedgerFactory{}
	lf.On("GetOrCreate", "mychannel").Return(lw, nil)

	osn := newClusterNode(t)
	defer osn.stop()

	enqueueBlock := func(seq int) {
		osn.blockResponses <- &orderer.DeliverResponse{
			Type: &orderer.DeliverResponse_Block{
				Block: blockchain[seq],
			},
		}
	}

	dialer := newCountingDialer()
	bp := newBlockPuller(dialer, osn.srv.Address())
	bp.FetchTimeout = time.Hour

	r := cluster.Replicator{
		Filter: cluster.AnyChannel,
		AmIPartOfChannel: func(configBlock *common.Block) error {
			return nil
		},
		Logger:        flogging.MustGetLogger("test"),
		SystemChannel: "system",
		LedgerFactory: lf,
		Puller:        bp,
	}

	var detectedChannelPulled bool
	r.Logger = r.Logger.WithOptions(zap.Hooks(func(entry zapcore.Entry) error {
		if strings.Contains(entry.Message, "Latest height found (6) is equal to our height, skipping pulling channel mychannel") {
			detectedChannelPulled = true
		}
		return nil
	}))

	osn.addExpectProbeAssert()
	enqueueBlock(5)
	osn.addExpectProbeAssert()
	enqueueBlock(5)

	err := r.PullChannel("mychannel")
	assert.NoError(t, err)
	assert.True(t, detectedChannelPulled)
}

func TestBlockPullerFromConfigBlockGreenPath(t *testing.T) {
	for _, testCase := range []struct {
		description        string
		blockVerifiers     []cluster.BlockVerifier
		expectedLogMessage string
		iterations         int
	}{
		{
			description:        "Success",
			blockVerifiers:     []cluster.BlockVerifier{&cluster.NoopBlockVerifier{}},
			expectedLogMessage: "Got block [0] of size",
			iterations:         1,
		},
		{
			description: "Failure",
			iterations:  2,
			// First time it returns nil, second time returns like the success case
			blockVerifiers: []cluster.BlockVerifier{nil, &cluster.NoopBlockVerifier{}},
			expectedLogMessage: "Failed verifying received blocks: " +
				"couldn't acquire verifier for channel mychannel",
		},
	} {
		t.Run(testCase.description, func(t *testing.T) {
			testBlockPullerFromConfig(t, testCase.blockVerifiers,
				testCase.expectedLogMessage, testCase.iterations)
		})
	}
}

func TestNoopBlockVerifier(t *testing.T) {
	v := &cluster.NoopBlockVerifier{}
	assert.Nil(t, v.VerifyBlockSignature(nil, nil))
}

func injectGlobalOrdererEndpoint(t *testing.T, block *common.Block, endpoint string) {
	ordererAddresses := channelconfig.OrdererAddressesValue([]string{endpoint})
	// Unwrap the layers until we reach the orderer addresses
	env, err := protoutil.ExtractEnvelope(block, 0)
	assert.NoError(t, err)
	payload, err := protoutil.UnmarshalPayload(env.Payload)
	assert.NoError(t, err)
	confEnv, err := configtx.UnmarshalConfigEnvelope(payload.Data)
	assert.NoError(t, err)
	// Replace the orderer addresses
	confEnv.Config.ChannelGroup.Values[ordererAddresses.Key()] = &common.ConfigValue{
		Value:     protoutil.MarshalOrPanic(ordererAddresses.Value()),
		ModPolicy: "/Channel/Orderer/Admins",
	}
	// Remove the per org addresses, if applicable
	ordererGrps := confEnv.Config.ChannelGroup.Groups[channelconfig.OrdererGroupKey].Groups
	for _, grp := range ordererGrps {
		if grp.Values[channelconfig.EndpointsKey] == nil {
			continue
		}
		grp.Values[channelconfig.EndpointsKey].Value = nil
	}
	// And put it back into the block
	payload.Data = protoutil.MarshalOrPanic(confEnv)
	env.Payload = protoutil.MarshalOrPanic(payload)
	block.Data.Data[0] = protoutil.MarshalOrPanic(env)
}

func injectTLSCACert(t *testing.T, block *common.Block, tlsCA []byte) {
	// Unwrap the layers until we reach the TLS CA certificates
	env, err := protoutil.ExtractEnvelope(block, 0)
	assert.NoError(t, err)
	payload, err := protoutil.UnmarshalPayload(env.Payload)
	assert.NoError(t, err)
	confEnv, err := configtx.UnmarshalConfigEnvelope(payload.Data)
	assert.NoError(t, err)
	mspKey := confEnv.Config.ChannelGroup.Groups[channelconfig.OrdererGroupKey].Groups["OrdererOrg"].Values[channelconfig.MSPKey]
	rawMSPConfig := mspKey.Value
	mspConf := &msp.MSPConfig{}
	proto.Unmarshal(rawMSPConfig, mspConf)
	fabricMSPConf := &msp.FabricMSPConfig{}
	proto.Unmarshal(mspConf.Config, fabricMSPConf)
	// Replace the TLS root certs with the given ones
	fabricMSPConf.TlsRootCerts = [][]byte{tlsCA}
	// And put it back into the block
	mspConf.Config = protoutil.MarshalOrPanic(fabricMSPConf)
	mspKey.Value = protoutil.MarshalOrPanic(mspConf)
	payload.Data = protoutil.MarshalOrPanic(confEnv)
	env.Payload = protoutil.MarshalOrPanic(payload)
	block.Data.Data[0] = protoutil.MarshalOrPanic(env)
}

func TestExtractGenesisBlock(t *testing.T) {
	for _, testCase := range []struct {
		name               string
		expectedErr        string
		returnedName       string
		block              *common.Block
		returnGenesisBlock bool
	}{
		{
			name:        "nil block",
			expectedErr: "nil block",
		},
		{
			name:        "no data section in block",
			expectedErr: "block data is nil",
			block:       &common.Block{},
		},
		{
			name: "corrupt envelope in block",
			expectedErr: "block data does not carry an" +
				" envelope at index 0: error unmarshaling Envelope: proto: common.Envelope: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{{1, 2, 3}},
				},
			},
		},
		{
			name:        "corrupt payload in envelope",
			expectedErr: "error unmarshaling Payload: proto: common.Payload: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: []byte{1, 2, 3},
					})},
				},
			},
		},
		{
			name:        "no header in block",
			expectedErr: "nil header in payload",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{}),
					})},
				},
			},
		},
		{
			name: "corrupt channel header",
			expectedErr: "error unmarshaling ChannelHeader:" +
				" proto: common.ChannelHeader: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: []byte{1, 2, 3},
							},
						}),
					})},
				},
			},
		},
		{
			name:        "not an orderer transaction",
			expectedErr: "",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_CONFIG_UPDATE),
								}),
							},
						}),
					})},
				},
			},
		},
		{
			name:        "orderer transaction with corrupt inner envelope",
			expectedErr: "error unmarshaling Envelope: proto: common.Envelope: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: []byte{1, 2, 3},
						}),
					})},
				},
			},
		},
		{
			name:        "orderer transaction with corrupt inner payload",
			expectedErr: "error unmarshaling Payload: proto: common.Payload: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: []byte{1, 2, 3},
							}),
						}),
					})},
				},
			},
		},
		{
			name:        "orderer transaction with nil inner header",
			expectedErr: "inner payload's header is nil",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: protoutil.MarshalOrPanic(&common.Payload{}),
							}),
						}),
					})},
				},
			},
		},
		{
			name:        "orderer transaction with corrupt inner channel header",
			expectedErr: "error unmarshaling ChannelHeader: proto: common.ChannelHeader: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: protoutil.MarshalOrPanic(&common.Payload{
									Header: &common.Header{
										ChannelHeader: []byte{1, 2, 3},
									},
								}),
							}),
						}),
					})},
				},
			},
		},
		{
			name:        "orderer transaction that is not a config, but a config update",
			expectedErr: "",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									Type: int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: protoutil.MarshalOrPanic(&common.Payload{
									Header: &common.Header{
										ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
											Type: int32(common.HeaderType_CONFIG_UPDATE),
										}),
									},
								}),
							}),
						}),
					})},
				},
			},
		},
		{
			expectedErr: "",
			name:        "orderer transaction that is a system channel config block",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									ChannelId: "systemChannel",
									Type:      int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: protoutil.MarshalOrPanic(&common.Payload{
									Header: &common.Header{
										ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
											Type:      int32(common.HeaderType_CONFIG),
											ChannelId: "systemChannel",
										}),
									},
								}),
							}),
						}),
					})},
				},
			},
		},
		{
			name:         "orderer transaction that creates a new application channel",
			expectedErr:  "",
			returnedName: "notSystemChannel",
			block: &common.Block{
				Data: &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
						Payload: protoutil.MarshalOrPanic(&common.Payload{
							Header: &common.Header{
								ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
									ChannelId: "systemChannel",
									Type:      int32(common.HeaderType_ORDERER_TRANSACTION),
								}),
							},
							Data: protoutil.MarshalOrPanic(&common.Envelope{
								Payload: protoutil.MarshalOrPanic(&common.Payload{
									Header: &common.Header{
										ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
											Type:      int32(common.HeaderType_CONFIG),
											ChannelId: "notSystemChannel",
										}),
									},
								}),
							}),
						}),
					})},
				},
			},
			returnGenesisBlock: true,
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			channelName, gb, err := cluster.ExtractGenesisBlock(flogging.MustGetLogger("test"), testCase.block)
			if testCase.expectedErr != "" {
				assert.EqualError(t, err, testCase.expectedErr)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, testCase.returnedName, channelName)
			if testCase.returnGenesisBlock {
				assert.NotNil(t, gb)
			} else {
				assert.Nil(t, gb)
			}
		})
	}
}

func TestChannels(t *testing.T) {
	makeBlock := func(outerChannelName, innerChannelName string) *common.Block {
		return &common.Block{
			Header: &common.BlockHeader{},
			Data: &common.BlockData{
				Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
					Payload: protoutil.MarshalOrPanic(&common.Payload{
						Header: &common.Header{
							ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
								ChannelId: outerChannelName,
								Type:      int32(common.HeaderType_ORDERER_TRANSACTION),
							}),
						},
						Data: protoutil.MarshalOrPanic(&common.Envelope{
							Payload: protoutil.MarshalOrPanic(&common.Payload{
								Header: &common.Header{
									ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
										Type:      int32(common.HeaderType_CONFIG),
										ChannelId: innerChannelName,
									}),
								},
							}),
						}),
					}),
				})},
			},
		}
	}

	for _, testCase := range []struct {
		name               string
		prepareSystemChain func(systemChain []*common.Block)
		assertion          func(t *testing.T, ci *cluster.ChainInspector)
	}{
		{
			name: "happy path - artificial blocks",
			prepareSystemChain: func(systemChain []*common.Block) {
				assignHashes(systemChain)
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				actual := cluster.GenesisBlocks(ci.Channels())
				// Assert that the returned channels are returned in any order
				assert.Contains(t, [][]string{{"mychannel", "mychannel2"}, {"mychannel2", "mychannel"}}, actual.Names())
			},
		},
		{
			name: "happy path - one block is not artificial but real",
			prepareSystemChain: func(systemChain []*common.Block) {
				blockbytes, err := ioutil.ReadFile(filepath.Join("testdata", "block3.pb"))
				assert.NoError(t, err)
				block := &common.Block{}
				err = proto.Unmarshal(blockbytes, block)
				assert.NoError(t, err)

				systemChain[len(systemChain)/2-1] = block
				assignHashes(systemChain)
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				actual := cluster.GenesisBlocks(ci.Channels())
				// Assert that the returned channels are returned in any order
				assert.Contains(t, [][]string{{"mychannel2", "bar"}, {"bar", "mychannel2"}}, actual.Names())
			},
		},
		{
			name: "bad path - pulled chain's last block hash doesn't match the last config block",
			prepareSystemChain: func(systemChain []*common.Block) {
				assignHashes(systemChain)
				systemChain[len(systemChain)-1].Header.PreviousHash = nil
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				panicValue := "System channel pulled doesn't match the boot last config block:" +
					" block [2]'s hash (bc4ef5cc8a61ac0747cc82df58bac9ad3278622c1cfc7a119b9b1068e422c9f1)" +
					" mismatches block [3]'s prev block hash ()"
				assert.PanicsWithValue(t, panicValue, func() {
					ci.Channels()
				})
			},
		},
		{
			name: "bad path - hash chain mismatch",
			prepareSystemChain: func(systemChain []*common.Block) {
				assignHashes(systemChain)
				systemChain[len(systemChain)-2].Header.PreviousHash = nil
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				panicValue := "Claimed previous hash of block [2] is  but actual previous " +
					"hash is 920faeb0bd8a02b3f2553247359fb3b684819c75c6e5487bc7eed632841ddc5f"
				assert.PanicsWithValue(t, panicValue, func() {
					ci.Channels()
				})
			},
		},
		{
			name: "bad path - a block cannot be classified",
			prepareSystemChain: func(systemChain []*common.Block) {
				assignHashes(systemChain)
				systemChain[len(systemChain)-2].Data.Data = [][]byte{{1, 2, 3}}
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				panicValue := "Failed extracting channel genesis block from config block: " +
					"block data does not carry an envelope at index 0: error unmarshaling " +
					"Envelope: proto: common.Envelope: illegal tag 0 (wire type 1)"
				assert.PanicsWithValue(t, panicValue, func() {
					ci.Channels()
				})
			},
		},
		{
			name: "bad path - failed pulling blocks",
			prepareSystemChain: func(systemChain []*common.Block) {
				assignHashes(systemChain)
				// Setting a block to nil makes the block puller return nil,
				// which signals failure of pulling a block.
				systemChain[len(systemChain)/2] = nil
			},
			assertion: func(t *testing.T, ci *cluster.ChainInspector) {
				panicValue := "Failed pulling block [2] from the system channel"
				assert.PanicsWithValue(t, panicValue, func() {
					ci.Channels()
				})
			},
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			systemChain := []*common.Block{
				makeBlock("systemChannel", "systemChannel"),
				makeBlock("systemChannel", "mychannel"),
				makeBlock("systemChannel", "mychannel2"),
				makeBlock("systemChannel", "systemChannel"),
			}

			for i := 0; i < len(systemChain); i++ {
				systemChain[i].Header.DataHash = protoutil.BlockDataHash(systemChain[i].Data)
				systemChain[i].Header.Number = uint64(i)
			}
			testCase.prepareSystemChain(systemChain)
			puller := &mocks.ChainPuller{}
			puller.On("Close")
			for seq := uint64(0); int(seq) < len(systemChain)-1; seq++ {
				puller.On("PullBlock", seq).Return(systemChain[int(seq)])
			}

			ci := &cluster.ChainInspector{
				Logger:          flogging.MustGetLogger("test"),
				Puller:          puller,
				LastConfigBlock: systemChain[len(systemChain)-1],
			}
			defer puller.AssertNumberOfCalls(t, "Close", 1)
			defer ci.Close()
			testCase.assertion(t, ci)
		})
	}
}

var fakeGB = &common.Block{
	Header: &common.BlockHeader{},
	Metadata: &common.BlockMetadata{
		Metadata: [][]byte{{}, {}, {}, {}},
	},
	Data: &common.BlockData{
		Data: [][]byte{
			protoutil.MarshalOrPanic(&common.Envelope{
				Payload: protoutil.MarshalOrPanic(&common.Envelope{
					Payload: protoutil.MarshalOrPanic(&common.Config{
						Sequence: 1,
					}),
				}),
			}),
		},
	},
}

func simulateNonParticipantChannelPull(osn *deliverServer) {
	lastBlock := protoutil.NewBlock(1, nil)
	lastBlock.Metadata.Metadata[common.BlockMetadataIndex_LAST_CONFIG] = protoutil.MarshalOrPanic(&common.Metadata{
		Value: protoutil.MarshalOrPanic(&common.LastConfig{Index: 0}),
	})
	// We first present a channel with a last block of 'lastBlock', that points to
	// the genesis block
	osn.addExpectProbeAssert()
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: lastBlock},
	}
	osn.addExpectProbeAssert()
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: lastBlock},
	}
	osn.addExpectPullAssert(1)
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: lastBlock},
	}
	osn.blockResponses <- nil

	// and make it send back the genesis block.
	// First send is for probing,
	osn.addExpectProbeAssert()
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: fakeGB},
	}
	osn.addExpectPullAssert(0)
	// and the second one sends the actual block itself downstream
	osn.blockResponses <- &orderer.DeliverResponse{
		Type: &orderer.DeliverResponse_Block{Block: fakeGB},
	}

	osn.blockResponses <- nil
}

func TestFilter(t *testing.T) {
	logger := flogging.MustGetLogger("test")
	logger = logger.WithOptions(zap.Hooks(func(entry zapcore.Entry) error {
		assert.Equal(t, "Channel foo shouldn't be pulled. Skipping it", entry.Message)
		return nil
	}))

	r := &cluster.Replicator{
		Filter: func(_ string) bool {
			return false
		},
		Logger: logger,
	}
	assert.Equal(t, cluster.ErrSkipped, r.PullChannel("foo"))
}
```

## `orderer/common/cluster/util.go` (changed lines (321, 322, 323, 324, 325))

```go
/*
Copyright IBM Corp. 2017 All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package cluster

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"math/rand"
	"sync"
	"sync/atomic"
	"time"

	"github.com/hyperledger/fabric-config/protolator"
	"github.com/hyperledger/fabric-protos-go/common"
	"github.com/hyperledger/fabric/bccsp"
	"github.com/hyperledger/fabric/common/channelconfig"
	"github.com/hyperledger/fabric/common/configtx"
	"github.com/hyperledger/fabric/common/flogging"
	"github.com/hyperledger/fabric/common/policies"
	"github.com/hyperledger/fabric/common/util"
	"github.com/hyperledger/fabric/internal/pkg/comm"
	"github.com/hyperledger/fabric/protoutil"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
)

// ConnByCertMap maps certificates represented as strings
// to gRPC connections
type ConnByCertMap map[string]*grpc.ClientConn

// Lookup looks up a certificate and returns the connection that was mapped
// to the certificate, and whether it was found or not
func (cbc ConnByCertMap) Lookup(cert []byte) (*grpc.ClientConn, bool) {
	conn, ok := cbc[string(cert)]
	return conn, ok
}

// Put associates the given connection to the certificate
func (cbc ConnByCertMap) Put(cert []byte, conn *grpc.ClientConn) {
	cbc[string(cert)] = conn
}

// Remove removes the connection that is associated to the given certificate
func (cbc ConnByCertMap) Remove(cert []byte) {
	delete(cbc, string(cert))
}

// Size returns the size of the connections by certificate mapping
func (cbc ConnByCertMap) Size() int {
	return len(cbc)
}

// CertificateComparator returns whether some relation holds for two given certificates
type CertificateComparator func([]byte, []byte) bool

// MemberMapping defines NetworkMembers by their ID
// and enables to lookup stubs by a certificate
type MemberMapping struct {
	id2stub       map[uint64]*Stub
	SamePublicKey CertificateComparator
}

// Foreach applies the given function on all stubs in the mapping
func (mp *MemberMapping) Foreach(f func(id uint64, stub *Stub)) {
	for id, stub := range mp.id2stub {
		f(id, stub)
	}
}

// Put inserts the given stub to the MemberMapping
func (mp *MemberMapping) Put(stub *Stub) {
	mp.id2stub[stub.ID] = stub
}

// Remove removes the stub with the given ID from the MemberMapping
func (mp *MemberMapping) Remove(ID uint64) {
	delete(mp.id2stub, ID)
}

// ByID retrieves the Stub with the given ID from the MemberMapping
func (mp MemberMapping) ByID(ID uint64) *Stub {
	return mp.id2stub[ID]
}

// LookupByClientCert retrieves a Stub with the given client certificate
func (mp MemberMapping) LookupByClientCert(cert []byte) *Stub {
	for _, stub := range mp.id2stub {
		if mp.SamePublicKey(stub.ClientTLSCert, cert) {
			return stub
		}
	}
	return nil
}

// ServerCertificates returns a set of the server certificates
// represented as strings
func (mp MemberMapping) ServerCertificates() StringSet {
	res := make(StringSet)
	for _, member := range mp.id2stub {
		res[string(member.ServerTLSCert)] = struct{}{}
	}
	return res
}

// StringSet is a set of strings
type StringSet map[string]struct{}

// union adds the elements of the given set to the StringSet
func (ss StringSet) union(set StringSet) {
	for k := range set {
		ss[k] = struct{}{}
	}
}

// subtract removes all elements in the given set from the StringSet
func (ss StringSet) subtract(set StringSet) {
	for k := range set {
		delete(ss, k)
	}
}

// PredicateDialer creates gRPC connections
// that are only established if the given predicate
// is fulfilled
type PredicateDialer struct {
	lock   sync.RWMutex
	Config comm.ClientConfig
}

func (dialer *PredicateDialer) UpdateRootCAs(serverRootCAs [][]byte) {
	dialer.lock.Lock()
	defer dialer.lock.Unlock()
	dialer.Config.SecOpts.ServerRootCAs = serverRootCAs
}

// Dial creates a new gRPC connection that can only be established, if the remote node's
// certificate chain satisfy verifyFunc
func (dialer *PredicateDialer) Dial(address string, verifyFunc RemoteVerifier) (*grpc.ClientConn, error) {
	dialer.lock.RLock()
	cfg := dialer.Config.Clone()
	dialer.lock.RUnlock()

	cfg.SecOpts.VerifyCertificate = verifyFunc
	client, err := comm.NewGRPCClient(cfg)
	if err != nil {
		return nil, errors.WithStack(err)
	}
	return client.NewConnection(address, func(tlsConfig *tls.Config) {
		// We need to dynamically overwrite the TLS root CAs,
		// as they may be updated.
		dialer.lock.RLock()
		serverRootCAs := dialer.Config.Clone().SecOpts.ServerRootCAs
		dialer.lock.RUnlock()

		tlsConfig.RootCAs = x509.NewCertPool()
		for _, pem := range serverRootCAs {
			tlsConfig.RootCAs.AppendCertsFromPEM(pem)
		}
	})
}

// DERtoPEM returns a PEM representation of the DER
// encoded certificate
func DERtoPEM(der []byte) string {
	return string(pem.EncodeToMemory(&pem.Block{
		Type:  "CERTIFICATE",
		Bytes: der,
	}))
}

// StandardDialer wraps an ClientConfig, and provides
// a means to connect according to given EndpointCriteria.
type StandardDialer struct {
	Config comm.ClientConfig
}

// Dial dials an address according to the given EndpointCriteria
func (dialer *StandardDialer) Dial(endpointCriteria EndpointCriteria) (*grpc.ClientConn, error) {
	cfg := dialer.Config.Clone()
	cfg.SecOpts.ServerRootCAs = endpointCriteria.TLSRootCAs

	client, err := comm.NewGRPCClient(cfg)
	if err != nil {
		return nil, errors.Wrap(err, "failed creating gRPC client")
	}

	return client.NewConnection(endpointCriteria.Endpoint)
}

//go:generate mockery -dir . -name BlockVerifier -case underscore -output ./mocks/

// BlockVerifier verifies block signatures.
type BlockVerifier interface {
	// VerifyBlockSignature verifies a signature of a block.
	// It has an optional argument of a configuration envelope
	// which would make the block verification to use validation rules
	// based on the given configuration in the ConfigEnvelope.
	// If the config envelope passed is nil, then the validation rules used
	// are the ones that were applied at commit of previous blocks.
	VerifyBlockSignature(sd []*protoutil.SignedData, config *common.ConfigEnvelope) error
}

// BlockSequenceVerifier verifies that the given consecutive sequence
// of blocks is valid.
type BlockSequenceVerifier func(blocks []*common.Block, channel string) error

// Dialer creates a gRPC connection to a remote address
type Dialer interface {
	Dial(endpointCriteria EndpointCriteria) (*grpc.ClientConn, error)
}

// VerifyBlocks verifies the given consecutive sequence of blocks is valid,
// and returns nil if it's valid, else an error.
func VerifyBlocks(blockBuff []*common.Block, signatureVerifier BlockVerifier) error {
	if len(blockBuff) == 0 {
		return errors.New("buffer is empty")
	}
	// First, we verify that the block hash in every block is:
	// Equal to the hash in the header
	// Equal to the previous hash in the succeeding block
	for i := range blockBuff {
		if err := VerifyBlockHash(i, blockBuff); err != nil {
			return err
		}
	}

	var config *common.ConfigEnvelope
	var isLastBlockConfigBlock bool
	// Verify all configuration blocks that are found inside the block batch,
	// with the configuration that was committed (nil) or with one that is picked up
	// during iteration over the block batch.
	for _, block := range blockBuff {
		configFromBlock, err := ConfigFromBlock(block)
		if err == errNotAConfig {
			isLastBlockConfigBlock = false
			continue
		}
		if err != nil {
			return err
		}
		// The block is a configuration block, so verify it
		if err := VerifyBlockSignature(block, signatureVerifier, config); err != nil {
			return err
		}
		config = configFromBlock
		isLastBlockConfigBlock = true
	}

	// Verify the last block's signature
	lastBlock := blockBuff[len(blockBuff)-1]

	// If last block is a config block, we verified it using the policy of the previous block, so it's valid.
	if isLastBlockConfigBlock {
		return nil
	}

	return VerifyBlockSignature(lastBlock, signatureVerifier, config)
}

var errNotAConfig = errors.New("not a config block")

// ConfigFromBlock returns a ConfigEnvelope if exists, or a *NotAConfigBlock error.
// It may also return some other error in case parsing failed.
func ConfigFromBlock(block *common.Block) (*common.ConfigEnvelope, error) {
	if block == nil || block.Data == nil || len(block.Data.Data) == 0 {
		return nil, errors.New("empty block")
	}
	txn := block.Data.Data[0]
	env, err := protoutil.GetEnvelopeFromBlock(txn)
	if err != nil {
		return nil, errors.WithStack(err)
	}
	payload, err := protoutil.UnmarshalPayload(env.Payload)
	if err != nil {
		return nil, errors.WithStack(err)
	}
	if block.Header.Number == 0 {
		configEnvelope, err := configtx.UnmarshalConfigEnvelope(payload.Data)
		if err != nil {
			return nil, errors.Wrap(err, "invalid config envelope")
		}
		return configEnvelope, nil
	}
	if payload.Header == nil {
		return nil, errors.New("nil header in payload")
	}
	chdr, err := protoutil.UnmarshalChannelHeader(payload.Header.ChannelHeader)
	if err != nil {
		return nil, errors.WithStack(err)
	}
	if common.HeaderType(chdr.Type) != common.HeaderType_CONFIG {
		return nil, errNotAConfig
	}
	configEnvelope, err := configtx.UnmarshalConfigEnvelope(payload.Data)
	if err != nil {
		return nil, errors.Wrap(err, "invalid config envelope")
	}
	return configEnvelope, nil
}

// VerifyBlockHash verifies the hash chain of the block with the given index
// among the blocks of the given block buffer.
func VerifyBlockHash(indexInBuffer int, blockBuff []*common.Block) error {
	if len(blockBuff) <= indexInBuffer {
		return errors.Errorf("index %d out of bounds (total %d blocks)", indexInBuffer, len(blockBuff))
	}
	block := blockBuff[indexInBuffer]
	if block.Header == nil {
		return errors.New("missing block header")
	}
	seq := block.Header.Number

	if err := protoutil.VerifyTransactionsAreWellFormed(block); err != nil && block.Header.Number > 0 {
		return fmt.Errorf("block has malformed transactions: %v", err)
	}

	dataHash := protoutil.BlockDataHash(block.Data)
	// Verify data hash matches the hash in the header
	if !bytes.Equal(dataHash, block.Header.DataHash) {
		computedHash := hex.EncodeToString(dataHash)
		claimedHash := hex.EncodeToString(block.Header.DataHash)
		return errors.Errorf("computed hash of block (%d) (%s) doesn't match claimed hash (%s)",
			seq, computedHash, claimedHash)
	}
	// We have a previous block in the buffer, ensure current block's previous hash matches the previous one.
	if indexInBuffer > 0 {
		prevBlock := blockBuff[indexInBuffer-1]
		currSeq := block.Header.Number
		if prevBlock.Header == nil {
			return errors.New("previous block header is nil")
		}
		prevSeq := prevBlock.Header.Number
		if prevSeq+1 != currSeq {
			return errors.Errorf("sequences %d and %d were received consecutively", prevSeq, currSeq)
		}
		if !bytes.Equal(block.Header.PreviousHash, protoutil.BlockHeaderHash(prevBlock.Header)) {
			claimedPrevHash := hex.EncodeToString(block.Header.PreviousHash)
			actualPrevHash := hex.EncodeToString(protoutil.BlockHeaderHash(prevBlock.Header))
			return errors.Errorf("block [%d]'s hash (%s) mismatches block [%d]'s prev block hash (%s)",
				prevSeq, actualPrevHash, currSeq, claimedPrevHash)
		}
	}
	return nil
}

// SignatureSetFromBlock creates a signature set out of a block.
func SignatureSetFromBlock(block *common.Block) ([]*protoutil.SignedData, error) {
	if block.Metadata == nil || len(block.Metadata.Metadata) <= int(common.BlockMetadataIndex_SIGNATURES) {
		return nil, errors.New("no metadata in block")
	}
	metadata, err := protoutil.GetMetadataFromBlock(block, common.BlockMetadataIndex_SIGNATURES)
	if err != nil {
		return nil, errors.Errorf("failed unmarshaling medatata for signatures: %v", err)
	}

	var signatureSet []*protoutil.SignedData
	for _, metadataSignature := range metadata.Signatures {
		sigHdr, err := protoutil.UnmarshalSignatureHeader(metadataSignature.SignatureHeader)
		if err != nil {
			return nil, errors.Errorf("failed unmarshaling signature header for block with id %d: %v",
				block.Header.Number, err)
		}
		signatureSet = append(signatureSet,
			&protoutil.SignedData{
				Identity: sigHdr.Creator,
				Data: util.ConcatenateBytes(metadata.Value,
					metadataSignature.SignatureHeader, protoutil.BlockHeaderBytes(block.Header)),
				Signature: metadataSignature.Signature,
			},
		)
	}
	return signatureSet, nil
}

// VerifyBlockSignature verifies the signature on the block with the given BlockVerifier and the given config.
func VerifyBlockSignature(block *common.Block, verifier BlockVerifier, config *common.ConfigEnvelope) error {
	signatureSet, err := SignatureSetFromBlock(block)
	if err != nil {
		return err
	}
	return verifier.VerifyBlockSignature(signatureSet, config)
}

// EndpointCriteria defines criteria of how to connect to a remote orderer node.
type EndpointCriteria struct {
	Endpoint   string   // Endpoint of the form host:port
	TLSRootCAs [][]byte // PEM encoded TLS root CA certificates
}

// String returns a string representation of this EndpointCriteria
func (ep EndpointCriteria) String() string {
	var formattedCAs []interface{}
	for _, rawCAFile := range ep.TLSRootCAs {
		var bl *pem.Block
		pemContent := rawCAFile
		for {
			bl, pemContent = pem.Decode(pemContent)
			if bl == nil {
				break
			}
			cert, err := x509.ParseCertificate(bl.Bytes)
			if err != nil {
				break
			}

			issuedBy := cert.Issuer.String()
			if cert.Issuer.String() == cert.Subject.String() {
				issuedBy = "self"
			}

			info := make(map[string]interface{})
			info["Expired"] = time.Now().After(cert.NotAfter)
			info["Subject"] = cert.Subject.String()
			info["Issuer"] = issuedBy
			formattedCAs = append(formattedCAs, info)
		}
	}

	formattedEndpointCriteria := make(map[string]interface{})
	formattedEndpointCriteria["Endpoint"] = ep.Endpoint
	formattedEndpointCriteria["CAs"] = formattedCAs

	rawJSON, err := json.Marshal(formattedEndpointCriteria)
	if err != nil {
		return fmt.Sprintf("{\"Endpoint\": \"%s\"}", ep.Endpoint)
	}

	return string(rawJSON)
}

// EndpointconfigFromConfigBlock retrieves TLS CA certificates and endpoints
// from a config block.
func EndpointconfigFromConfigBlock(block *common.Block, bccsp bccsp.BCCSP) ([]EndpointCriteria, error) {
	if block == nil {
		return nil, errors.New("nil block")
	}
	envelopeConfig, err := protoutil.ExtractEnvelope(block, 0)
	if err != nil {
		return nil, err
	}

	bundle, err := channelconfig.NewBundleFromEnvelope(envelopeConfig, bccsp)
	if err != nil {
		return nil, errors.Wrap(err, "failed extracting bundle from envelope")
	}
	msps, err := bundle.MSPManager().GetMSPs()
	if err != nil {
		return nil, errors.Wrap(err, "failed obtaining MSPs from MSPManager")
	}
	ordererConfig, ok := bundle.OrdererConfig()
	if !ok {
		return nil, errors.New("failed obtaining orderer config from bundle")
	}

	mspIDsToCACerts := make(map[string][][]byte)
	var aggregatedTLSCerts [][]byte
	for _, org := range ordererConfig.Organizations() {
		// Validate that every orderer org has a corresponding MSP instance in the MSP Manager.
		msp, exists := msps[org.MSPID()]
		if !exists {
			return nil, errors.Errorf("no MSP found for MSP with ID of %s", org.MSPID())
		}

		// Build a per org mapping of the TLS CA certs for this org,
		// and aggregate all TLS CA certs into aggregatedTLSCerts to be used later on.
		var caCerts [][]byte
		caCerts = append(caCerts, msp.GetTLSIntermediateCerts()...)
		caCerts = append(caCerts, msp.GetTLSRootCerts()...)
		mspIDsToCACerts[org.MSPID()] = caCerts
		aggregatedTLSCerts = append(aggregatedTLSCerts, caCerts...)
	}

	endpointsPerOrg := perOrgEndpoints(ordererConfig, mspIDsToCACerts)
	if len(endpointsPerOrg) > 0 {
		return endpointsPerOrg, nil
	}

	return globalEndpointsFromConfig(aggregatedTLSCerts, bundle), nil
}

func perOrgEndpoints(ordererConfig channelconfig.Orderer, mspIDsToCerts map[string][][]byte) []EndpointCriteria {
	var endpointsPerOrg []EndpointCriteria

	for _, org := range ordererConfig.Organizations() {
		for _, endpoint := range org.Endpoints() {
			endpointsPerOrg = append(endpointsPerOrg, EndpointCriteria{
				TLSRootCAs: mspIDsToCerts[org.MSPID()],
				Endpoint:   endpoint,
			})
		}
	}

	return endpointsPerOrg
}

func globalEndpointsFromConfig(aggregatedTLSCerts [][]byte, bundle *channelconfig.Bundle) []EndpointCriteria {
	var globalEndpoints []EndpointCriteria
	for _, endpoint := range bundle.ChannelConfig().OrdererAddresses() {
		globalEndpoints = append(globalEndpoints, EndpointCriteria{
			Endpoint:   endpoint,
			TLSRootCAs: aggregatedTLSCerts,
		})
	}
	return globalEndpoints
}

//go:generate mockery -dir . -name VerifierFactory -case underscore -output ./mocks/

// VerifierFactory creates BlockVerifiers.
type VerifierFactory interface {
	// VerifierFromConfig creates a BlockVerifier from the given configuration.
	VerifierFromConfig(configuration *common.ConfigEnvelope, channel string) (BlockVerifier, error)
}

// VerificationRegistry registers verifiers and retrieves them.
type VerificationRegistry struct {
	LoadVerifier       func(chain string) BlockVerifier
	Logger             *flogging.FabricLogger
	VerifierFactory    VerifierFactory
	VerifiersByChannel map[string]BlockVerifier
}

// RegisterVerifier adds a verifier into the registry if applicable.
func (vr *VerificationRegistry) RegisterVerifier(chain string) {
	if _, exists := vr.VerifiersByChannel[chain]; exists {
		vr.Logger.Debugf("No need to register verifier for chain %s", chain)
		return
	}

	v := vr.LoadVerifier(chain)
	if v == nil {
		vr.Logger.Errorf("Failed loading verifier for chain %s", chain)
		return
	}

	vr.VerifiersByChannel[chain] = v
	vr.Logger.Infof("Registered verifier for chain %s", chain)
}

// RetrieveVerifier returns a BlockVerifier for the given channel, or nil if not found.
func (vr *VerificationRegistry) RetrieveVerifier(channel string) BlockVerifier {
	verifier, exists := vr.VerifiersByChannel[channel]
	if exists {
		return verifier
	}
	vr.Logger.Errorf("No verifier for channel %s exists", channel)
	return nil
}

// BlockCommitted notifies the VerificationRegistry upon a block commit, which may
// trigger a registration of a verifier out of the block in case the block is a config block.
func (vr *VerificationRegistry) BlockCommitted(block *common.Block, channel string) {
	conf, err := ConfigFromBlock(block)
	// The block doesn't contain a config block, but is a valid block
	if err == errNotAConfig {
		vr.Logger.Debugf("Committed block [%d] for channel %s that is not a config block",
			block.Header.Number, channel)
		return
	}
	// The block isn't a valid block
	if err != nil {
		vr.Logger.Errorf("Failed parsing block of channel %s: %v, content: %s",
			channel, err, BlockToString(block))
		return
	}

	// The block contains a config block
	verifier, err := vr.VerifierFactory.VerifierFromConfig(conf, channel)
	if err != nil {
		vr.Logger.Errorf("Failed creating a verifier from a config block for channel %s: %v, content: %s",
			channel, err, BlockToString(block))
		return
	}

	vr.VerifiersByChannel[channel] = verifier

	vr.Logger.Debugf("Committed config block [%d] for channel %s", block.Header.Number, channel)
}

// BlockToString returns a string representation of this block.
func BlockToString(block *common.Block) string {
	buff := &bytes.Buffer{}
	protolator.DeepMarshalJSON(buff, block)
	return buff.String()
}

// BlockCommitFunc signals a block commit.
type BlockCommitFunc func(block *common.Block, channel string)

// LedgerInterceptor intercepts block commits.
type LedgerInterceptor struct {
	Channel              string
	InterceptBlockCommit BlockCommitFunc
	LedgerWriter
}

// Append commits a block into the ledger, and also fires the configured callback.
func (interceptor *LedgerInterceptor) Append(block *common.Block) error {
	defer interceptor.InterceptBlockCommit(block, interceptor.Channel)
	return interceptor.LedgerWriter.Append(block)
}

// BlockVerifierAssembler creates a BlockVerifier out of a config envelope
type BlockVerifierAssembler struct {
	Logger *flogging.FabricLogger
	BCCSP  bccsp.BCCSP
}

// VerifierFromConfig creates a BlockVerifier from the given configuration.
func (bva *BlockVerifierAssembler) VerifierFromConfig(configuration *common.ConfigEnvelope, channel string) (BlockVerifier, error) {
	bundle, err := channelconfig.NewBundle(channel, configuration.Config, bva.BCCSP)
	if err != nil {
		return nil, errors.Wrap(err, "failed extracting bundle from envelope")
	}
	policyMgr := bundle.PolicyManager()

	return &BlockValidationPolicyVerifier{
		Logger:    bva.Logger,
		PolicyMgr: policyMgr,
		Channel:   channel,
		BCCSP:     bva.BCCSP,
	}, nil
}

// BlockValidationPolicyVerifier verifies signatures based on the block validation policy.
type BlockValidationPolicyVerifier struct {
	Logger    *flogging.FabricLogger
	Channel   string
	PolicyMgr policies.Manager
	BCCSP     bccsp.BCCSP
}

// VerifyBlockSignature verifies the signed data associated to a block, optionally with the given config envelope.
func (bv *BlockValidationPolicyVerifier) VerifyBlockSignature(sd []*protoutil.SignedData, envelope *common.ConfigEnvelope) error {
	policyMgr := bv.PolicyMgr
	// If the envelope passed isn't nil, we should use a different policy manager.
	if envelope != nil {
		bundle, err := channelconfig.NewBundle(bv.Channel, envelope.Config, bv.BCCSP)
		if err != nil {
			buff := &bytes.Buffer{}
			protolator.DeepMarshalJSON(buff, envelope.Config)
			bv.Logger.Errorf("Failed creating a new bundle for channel %s, Config content is: %s", bv.Channel, buff.String())
			return err
		}
		bv.Logger.Infof("Initializing new PolicyManager for channel %s", bv.Channel)
		policyMgr = bundle.PolicyManager()
	}
	policy, exists := policyMgr.GetPolicy(policies.BlockValidation)
	if !exists {
		return errors.Errorf("policy %s wasn't found", policies.BlockValidation)
	}
	return policy.EvaluateSignedData(sd)
}

//go:generate mockery -dir . -name BlockRetriever -case underscore -output ./mocks/

// BlockRetriever retrieves blocks
type BlockRetriever interface {
	// Block returns a block with the given number,
	// or nil if such a block doesn't exist.
	Block(number uint64) *common.Block
}

// LastConfigBlock returns the last config block relative to the given block.
func LastConfigBlock(block *common.Block, blockRetriever BlockRetriever) (*common.Block, error) {
	if block == nil {
		return nil, errors.New("nil block")
	}
	if blockRetriever == nil {
		return nil, errors.New("nil blockRetriever")
	}
	lastConfigBlockNum, err := protoutil.GetLastConfigIndexFromBlock(block)
	if err != nil {
		return nil, err
	}
	lastConfigBlock := blockRetriever.Block(lastConfigBlockNum)
	if lastConfigBlock == nil {
		return nil, errors.Errorf("unable to retrieve last config block [%d]", lastConfigBlockNum)
	}
	return lastConfigBlock, nil
}

// StreamCountReporter reports the number of streams currently connected to this node
type StreamCountReporter struct {
	Metrics *Metrics
	count   uint32
}

func (scr *StreamCountReporter) Increment() {
	count := atomic.AddUint32(&scr.count, 1)
	scr.Metrics.reportStreamCount(count)
}

func (scr *StreamCountReporter) Decrement() {
	count := atomic.AddUint32(&scr.count, ^uint32(0))
	scr.Metrics.reportStreamCount(count)
}

type certificateExpirationCheck struct {
	minimumExpirationWarningInterval time.Duration
	expiresAt                        time.Time
	expirationWarningThreshold       time.Duration
	lastWarning                      time.Time
	nodeName                         string
	endpoint                         string
	alert                            func(string, ...interface{})
}

func (exp *certificateExpirationCheck) checkExpiration(currentTime time.Time, channel string) {
	timeLeft := exp.expiresAt.Sub(currentTime)
	if timeLeft > exp.expirationWarningThreshold {
		return
	}

	timeSinceLastWarning := currentTime.Sub(exp.lastWarning)
	if timeSinceLastWarning < exp.minimumExpirationWarningInterval {
		return
	}

	exp.alert("Certificate of %s from %s for channel %s expires in less than %v",
		exp.nodeName, exp.endpoint, channel, timeLeft)
	exp.lastWarning = currentTime
}

// CachePublicKeyComparisons creates CertificateComparator that caches invocations based on input arguments.
// The given CertificateComparator must be a stateless function.
func CachePublicKeyComparisons(f CertificateComparator) CertificateComparator {
	m := &ComparisonMemoizer{
		MaxEntries: 4096,
		F:          f,
	}
	return m.Compare
}

// ComparisonMemoizer speeds up comparison computations by caching past invocations of a stateless function
type ComparisonMemoizer struct {
	// Configuration
	F          func(a, b []byte) bool
	MaxEntries uint16
	// Internal state
	cache map[arguments]bool
	lock  sync.RWMutex
	once  sync.Once
	rand  *rand.Rand
}

type arguments struct {
	a, b string
}

// Size returns the number of computations the ComparisonMemoizer currently caches.
func (cm *ComparisonMemoizer) Size() int {
	cm.lock.RLock()
	defer cm.lock.RUnlock()
	return len(cm.cache)
}

// Compare compares the given two byte slices.
// It may return previous computations for the given two arguments,
// otherwise it will compute the function F and cache the result.
func (cm *ComparisonMemoizer) Compare(a, b []byte) bool {
	cm.once.Do(cm.setup)
	key := arguments{
		a: string(a),
		b: string(b),
	}

	cm.lock.RLock()
	result, exists := cm.cache[key]
	cm.lock.RUnlock()

	if exists {
		return result
	}

	result = cm.F(a, b)

	cm.lock.Lock()
	defer cm.lock.Unlock()

	cm.shrinkIfNeeded()
	cm.cache[key] = result

	return result
}

func (cm *ComparisonMemoizer) shrinkIfNeeded() {
	for {
		currentSize := uint16(len(cm.cache))
		if currentSize < cm.MaxEntries {
			return
		}
		cm.shrink()
	}
}

func (cm *ComparisonMemoizer) shrink() {
	// Shrink the cache by 25% by removing every fourth element (on average)
	for key := range cm.cache {
		if cm.rand.Int()%4 != 0 {
			continue
		}
		delete(cm.cache, key)
	}
}

func (cm *ComparisonMemoizer) setup() {
	cm.lock.Lock()
	defer cm.lock.Unlock()
	cm.rand = rand.New(rand.NewSource(time.Now().UnixNano()))
	cm.cache = make(map[arguments]bool)
}
```

## `orderer/common/cluster/util_test.go` (changed lines (292, 302, 376, 401, 553, 562, 564, 709, 710, 735, 736, 752, 753))

```go
/*
Copyright IBM Corp. 2017 All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package cluster_test

import (
	"bytes"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"fmt"
	"io/ioutil"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/hyperledger/fabric-protos-go/common"
	"github.com/hyperledger/fabric-protos-go/msp"
	"github.com/hyperledger/fabric/bccsp/sw"
	"github.com/hyperledger/fabric/common/capabilities"
	"github.com/hyperledger/fabric/common/channelconfig"
	"github.com/hyperledger/fabric/common/configtx"
	"github.com/hyperledger/fabric/common/configtx/test"
	"github.com/hyperledger/fabric/common/crypto/tlsgen"
	"github.com/hyperledger/fabric/common/flogging"
	"github.com/hyperledger/fabric/common/policies"
	"github.com/hyperledger/fabric/core/config/configtest"
	"github.com/hyperledger/fabric/internal/configtxgen/encoder"
	"github.com/hyperledger/fabric/internal/configtxgen/genesisconfig"
	"github.com/hyperledger/fabric/internal/pkg/comm"
	"github.com/hyperledger/fabric/orderer/common/cluster"
	"github.com/hyperledger/fabric/orderer/common/cluster/mocks"
	"github.com/hyperledger/fabric/protoutil"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/mock"
	"github.com/stretchr/testify/require"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

//go:generate counterfeiter -o mocks/policy.go --fake-name Policy . policy

type policy interface {
	policies.Policy
}

//go:generate counterfeiter -o mocks/policy_manager.go --fake-name PolicyManager . policyManager

type policyManager interface {
	policies.Manager
}

func TestParallelStubActivation(t *testing.T) {
	// Scenario: Activate the stub from different goroutines in parallel.
	stub := &cluster.Stub{}
	var wg sync.WaitGroup
	n := 100
	wg.Add(n)
	instance := &cluster.RemoteContext{}
	var activationCount int
	maybeCreateInstance := func() (*cluster.RemoteContext, error) {
		activationCount++
		return instance, nil
	}

	for i := 0; i < n; i++ {
		go func() {
			defer wg.Done()
			stub.Activate(maybeCreateInstance)
		}()
	}
	wg.Wait()
	activatedStub := stub.RemoteContext
	// Ensure the instance is the reference we stored
	// and not any other reference, i.e - it wasn't
	// copied by value.
	assert.True(t, activatedStub == instance)
	// Ensure the method was invoked only once.
	assert.Equal(t, activationCount, 1)
}

func TestDialerCustomKeepAliveOptions(t *testing.T) {
	ca, err := tlsgen.NewCA()
	assert.NoError(t, err)

	clientKeyPair, err := ca.NewClientCertKeyPair()
	assert.NoError(t, err)
	clientConfig := comm.ClientConfig{
		KaOpts: comm.KeepaliveOptions{
			ClientTimeout: time.Second * 12345,
		},
		Timeout: time.Millisecond * 100,
		SecOpts: comm.SecureOptions{
			RequireClientCert: true,
			Key:               clientKeyPair.Key,
			Certificate:       clientKeyPair.Cert,
			ServerRootCAs:     [][]byte{ca.CertBytes()},
			UseTLS:            true,
			ClientRootCAs:     [][]byte{ca.CertBytes()},
		},
	}

	dialer := &cluster.PredicateDialer{Config: clientConfig}
	timeout := dialer.Config.KaOpts.ClientTimeout
	assert.Equal(t, time.Second*12345, timeout)
}

func TestPredicateDialerUpdateRootCAs(t *testing.T) {
	node1 := newTestNode(t)
	defer node1.stop()

	anotherTLSCA, err := tlsgen.NewCA()
	assert.NoError(t, err)

	dialer := &cluster.PredicateDialer{
		Config: node1.clientConfig.Clone(),
	}
	dialer.Config.SecOpts.ServerRootCAs = [][]byte{anotherTLSCA.CertBytes()}
	dialer.Config.Timeout = time.Second
	dialer.Config.AsyncConnect = false

	_, err = dialer.Dial(node1.srv.Address(), nil)
	assert.Error(t, err)

	// Update root TLS CAs asynchronously to make sure we don't have a data race.
	go func() {
		dialer.UpdateRootCAs(node1.clientConfig.SecOpts.ServerRootCAs)
	}()

	// Eventually we should succeed connecting.
	for i := 0; i < 10; i++ {
		conn, err := dialer.Dial(node1.srv.Address(), nil)
		if err == nil {
			conn.Close()
			return
		}
	}

	assert.Fail(t, "could not connect after 10 attempts despite changing TLS CAs")
}

func TestDialerBadConfig(t *testing.T) {
	emptyCertificate := []byte("-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----")
	dialer := &cluster.PredicateDialer{
		Config: comm.ClientConfig{
			SecOpts: comm.SecureOptions{
				UseTLS:        true,
				ServerRootCAs: [][]byte{emptyCertificate},
			},
		},
	}
	_, err := dialer.Dial("127.0.0.1:8080", func(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error {
		return nil
	})
	assert.EqualError(t, err, "error adding root certificate: x509: malformed certificate")
}

func TestDERtoPEM(t *testing.T) {
	ca, err := tlsgen.NewCA()
	assert.NoError(t, err)
	keyPair, err := ca.NewServerCertKeyPair("localhost")
	assert.NoError(t, err)
	assert.Equal(t, cluster.DERtoPEM(keyPair.TLSCert.Raw), string(keyPair.Cert))
}

func TestStandardDialer(t *testing.T) {
	emptyCertificate := []byte("-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----")
	certPool := [][]byte{emptyCertificate}
	config := comm.ClientConfig{SecOpts: comm.SecureOptions{UseTLS: true, ServerRootCAs: certPool}}
	standardDialer := &cluster.StandardDialer{
		Config: config,
	}
	_, err := standardDialer.Dial(cluster.EndpointCriteria{Endpoint: "127.0.0.1:8080", TLSRootCAs: certPool})
	assert.EqualError(t,
		err,
		"failed creating gRPC client: error adding root certificate: x509: malformed certificate",
	)
}

func TestVerifyBlockSignature(t *testing.T) {
	verifier := &mocks.BlockVerifier{}
	var nilConfigEnvelope *common.ConfigEnvelope
	verifier.On("VerifyBlockSignature", mock.Anything, nilConfigEnvelope).Return(nil)

	block := createBlockChain(3, 3)[0]

	// The block should have a valid structure
	err := cluster.VerifyBlockSignature(block, verifier, nil)
	assert.NoError(t, err)

	for _, testCase := range []struct {
		name          string
		mutateBlock   func(*common.Block) *common.Block
		errorContains string
	}{
		{
			name:          "nil metadata",
			errorContains: "no metadata in block",
			mutateBlock: func(block *common.Block) *common.Block {
				block.Metadata = nil
				return block
			},
		},
		{
			name:          "zero metadata slice",
			errorContains: "no metadata in block",
			mutateBlock: func(block *common.Block) *common.Block {
				block.Metadata.Metadata = nil
				return block
			},
		},
		{
			name:          "nil metadata",
			errorContains: "failed unmarshaling medatata for signatures",
			mutateBlock: func(block *common.Block) *common.Block {
				block.Metadata.Metadata[0] = []byte{1, 2, 3}
				return block
			},
		},
		{
			name:          "bad signature header",
			errorContains: "failed unmarshaling signature header",
			mutateBlock: func(block *common.Block) *common.Block {
				metadata := protoutil.GetMetadataFromBlockOrPanic(block, common.BlockMetadataIndex_SIGNATURES)
				metadata.Signatures[0].SignatureHeader = []byte{1, 2, 3}
				block.Metadata.Metadata[common.BlockMetadataIndex_SIGNATURES] = protoutil.MarshalOrPanic(metadata)
				return block
			},
		},
	} {
		testCase := testCase
		t.Run(testCase.name, func(t *testing.T) {
			// Create a copy of the block
			blockCopy := &common.Block{}
			err := proto.Unmarshal(protoutil.MarshalOrPanic(block), blockCopy)
			assert.NoError(t, err)
			// Mutate the block to sabotage it
			blockCopy = testCase.mutateBlock(blockCopy)
			err = cluster.VerifyBlockSignature(blockCopy, verifier, nil)
			assert.Contains(t, err.Error(), testCase.errorContains)
		})
	}
}

func TestVerifyBlockHash(t *testing.T) {
	var start uint64 = 3
	var end uint64 = 23

	verify := func(blockchain []*common.Block) error {
		for i := 0; i < len(blockchain); i++ {
			err := cluster.VerifyBlockHash(i, blockchain)
			if err != nil {
				return err
			}
		}
		return nil
	}

	// Verify that createBlockChain() creates a valid blockchain
	assert.NoError(t, verify(createBlockChain(start, end)))

	twoBlocks := createBlockChain(2, 3)
	twoBlocks[0].Header = nil
	assert.EqualError(t, cluster.VerifyBlockHash(1, twoBlocks), "previous block header is nil")

	// Index out of bounds
	blockchain := createBlockChain(start, end)
	err := cluster.VerifyBlockHash(100, blockchain)
	assert.EqualError(t, err, "index 100 out of bounds (total 21 blocks)")

	for _, testCase := range []struct {
		name                string
		mutateBlockSequence func([]*common.Block) []*common.Block
		errorContains       string
	}{
		{
			name:          "non consecutive sequences",
			errorContains: "sequences 12 and 666 were received consecutively",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[len(blockSequence)/2].Header.Number = 666
				assignHashes(blockSequence)
				return blockSequence
			},
		},
		{
			name: "data hash mismatch",
			errorContains: "computed hash of block (13) (6de668ac99645e179a4921b477d50df9295fa56cd44f8e5c94756b60ce32ce1c)" +
				" doesn't match claimed hash (07)",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[len(blockSequence)/2].Header.DataHash = []byte{7}
				return blockSequence
			},
		},
		{
			name: "prev hash mismatch",
			errorContains: "block [12]'s hash " +
				"(72cc7ddf4d8465da95115c0a906416d23d8c74bfcb731a5ab057c213d8db62e1) " +
				"mismatches block [13]'s prev block hash (07)",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[len(blockSequence)/2].Header.PreviousHash = []byte{7}
				return blockSequence
			},
		},
		{
			name:          "nil block header",
			errorContains: "missing block header",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[0].Header = nil
				return blockSequence
			},
		},
	} {
		testCase := testCase
		t.Run(testCase.name, func(t *testing.T) {
			blockchain := createBlockChain(start, end)
			blockchain = testCase.mutateBlockSequence(blockchain)
			err := verify(blockchain)
			assert.EqualError(t, err, testCase.errorContains)
		})
	}
}

func TestVerifyBlocks(t *testing.T) {
	var sigSet1 []*protoutil.SignedData
	var sigSet2 []*protoutil.SignedData

	configEnvelope1 := &common.ConfigEnvelope{
		Config: &common.Config{
			Sequence: 1,
		},
	}
	configEnvelope2 := &common.ConfigEnvelope{
		Config: &common.Config{
			Sequence: 2,
		},
	}
	configTransaction := func(envelope *common.ConfigEnvelope) *common.Envelope {
		return &common.Envelope{
			Payload: protoutil.MarshalOrPanic(&common.Payload{
				Data: protoutil.MarshalOrPanic(envelope),
				Header: &common.Header{
					ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
						Type: int32(common.HeaderType_CONFIG),
					}),
				},
			}),
		}
	}

	for _, testCase := range []struct {
		name                  string
		configureVerifier     func(*mocks.BlockVerifier)
		mutateBlockSequence   func([]*common.Block) []*common.Block
		expectedError         string
		verifierExpectedCalls int
	}{
		{
			name: "empty sequence",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				return nil
			},
			expectedError: "buffer is empty",
		},
		{
			name: "prev hash mismatch",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[len(blockSequence)/2].Header.PreviousHash = []byte{7}
				return blockSequence
			},
			expectedError: "block [74]'s hash " +
				"(6daec1924ac6db2b23e3f49c190115dfc096603bcd0ec916baf111c68633c969) " +
				"mismatches block [75]'s prev block hash (07)",
		},
		{
			name: "bad signature",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				return blockSequence
			},
			configureVerifier: func(verifier *mocks.BlockVerifier) {
				var nilEnvelope *common.ConfigEnvelope
				verifier.On("VerifyBlockSignature", mock.Anything, nilEnvelope).Return(errors.New("bad signature"))
			},
			expectedError:         "bad signature",
			verifierExpectedCalls: 1,
		},
		{
			name: "block that its type cannot be classified",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				blockSequence[len(blockSequence)/2].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{})},
				}
				blockSequence[len(blockSequence)/2].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)/2].Data)
				assignHashes(blockSequence)
				return blockSequence
			},
			expectedError: "block has malformed transactions: transaction 0 has no payload",
		},
		{
			name: "config blocks in the sequence need to be verified and one of them is improperly signed",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				var err error
				// Put a config transaction in block n / 4
				blockSequence[len(blockSequence)/4].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(configTransaction(configEnvelope1))},
				}
				blockSequence[len(blockSequence)/4].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)/4].Data)

				// Put a config transaction in block n / 2
				blockSequence[len(blockSequence)/2].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(configTransaction(configEnvelope2))},
				}
				blockSequence[len(blockSequence)/2].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)/2].Data)

				assignHashes(blockSequence)

				sigSet1, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)/4])
				assert.NoError(t, err)
				sigSet2, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)/2])
				assert.NoError(t, err)

				return blockSequence
			},
			configureVerifier: func(verifier *mocks.BlockVerifier) {
				var nilEnvelope *common.ConfigEnvelope
				// The first config block, validates correctly.
				verifier.On("VerifyBlockSignature", sigSet1, nilEnvelope).Return(nil).Once()
				// However, the second config block - validates incorrectly.
				confEnv1 := &common.ConfigEnvelope{}
				proto.Unmarshal(protoutil.MarshalOrPanic(configEnvelope1), confEnv1)
				verifier.On("VerifyBlockSignature", sigSet2, confEnv1).Return(errors.New("bad signature")).Once()
			},
			expectedError:         "bad signature",
			verifierExpectedCalls: 2,
		},
		{
			name: "config block in the sequence needs to be verified, and it is properly signed",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				var err error
				// Put a config transaction in block n / 4
				blockSequence[len(blockSequence)/4].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(configTransaction(configEnvelope1))},
				}
				blockSequence[len(blockSequence)/4].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)/4].Data)

				assignHashes(blockSequence)

				sigSet1, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)/4])
				assert.NoError(t, err)

				sigSet2, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)-1])
				assert.NoError(t, err)

				return blockSequence
			},
			configureVerifier: func(verifier *mocks.BlockVerifier) {
				var nilEnvelope *common.ConfigEnvelope
				confEnv1 := &common.ConfigEnvelope{}
				proto.Unmarshal(protoutil.MarshalOrPanic(configEnvelope1), confEnv1)
				verifier.On("VerifyBlockSignature", sigSet1, nilEnvelope).Return(nil).Once()
				verifier.On("VerifyBlockSignature", sigSet2, confEnv1).Return(nil).Once()
			},
			// We have a single config block in the 'middle' of the chain, so we have 2 verifications total:
			// The last block, and the config block.
			verifierExpectedCalls: 2,
		},
		{
			name: "last two blocks are config blocks, last block only verified once",
			mutateBlockSequence: func(blockSequence []*common.Block) []*common.Block {
				var err error
				// Put a config transaction in block n-2 and in n-1
				blockSequence[len(blockSequence)-2].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(configTransaction(configEnvelope1))},
				}
				blockSequence[len(blockSequence)-2].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)-2].Data)

				blockSequence[len(blockSequence)-1].Data = &common.BlockData{
					Data: [][]byte{protoutil.MarshalOrPanic(configTransaction(configEnvelope2))},
				}
				blockSequence[len(blockSequence)-1].Header.DataHash = protoutil.BlockDataHash(blockSequence[len(blockSequence)-1].Data)

				assignHashes(blockSequence)

				sigSet1, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)-2])
				assert.NoError(t, err)

				sigSet2, err = cluster.SignatureSetFromBlock(blockSequence[len(blockSequence)-1])
				assert.NoError(t, err)

				return blockSequence
			},
			configureVerifier: func(verifier *mocks.BlockVerifier) {
				var nilEnvelope *common.ConfigEnvelope
				confEnv1 := &common.ConfigEnvelope{}
				proto.Unmarshal(protoutil.MarshalOrPanic(configEnvelope1), confEnv1)
				verifier.On("VerifyBlockSignature", sigSet1, nilEnvelope).Return(nil).Once()
				// We ensure that the signature set of the last block is verified using the config envelope of the block
				// before it.
				verifier.On("VerifyBlockSignature", sigSet2, confEnv1).Return(nil).Once()
				// Note that we do not record a call to verify the last block, with the config envelope extracted from the block itself.
			},
			// We have 2 config blocks, yet we only verify twice- the first config block, and the next config block, but no more,
			// since the last block is a config block.
			verifierExpectedCalls: 2,
		},
	} {
		testCase := testCase
		t.Run(testCase.name, func(t *testing.T) {
			blockchain := createBlockChain(50, 100)
			blockchain = testCase.mutateBlockSequence(blockchain)
			verifier := &mocks.BlockVerifier{}
			if testCase.configureVerifier != nil {
				testCase.configureVerifier(verifier)
			}
			err := cluster.VerifyBlocks(blockchain, verifier)
			if testCase.expectedError != "" {
				assert.EqualError(t, err, testCase.expectedError)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func assignHashes(blockchain []*common.Block) {
	for i := 1; i < len(blockchain); i++ {
		blockchain[i].Header.PreviousHash = protoutil.BlockHeaderHash(blockchain[i-1].Header)
	}
}

func createBlockChain(start, end uint64) []*common.Block {
	newBlock := func(seq uint64) *common.Block {
		sHdr := &common.SignatureHeader{
			Creator: []byte{1, 2, 3},
			Nonce:   []byte{9, 5, 42, 66},
		}
		block := protoutil.NewBlock(seq, nil)
		blockSignature := &common.MetadataSignature{
			SignatureHeader: protoutil.MarshalOrPanic(sHdr),
		}
		block.Metadata.Metadata[common.BlockMetadataIndex_SIGNATURES] = protoutil.MarshalOrPanic(&common.Metadata{
			Value: nil,
			Signatures: []*common.MetadataSignature{
				blockSignature,
			},
		})

		txn := protoutil.MarshalOrPanic(&common.Envelope{
			Signature: []byte{1, 2, 3},
			Payload: protoutil.MarshalOrPanic(&common.Payload{
				Header: &common.Header{},
			}),
		})
		block.Data.Data = append(block.Data.Data, txn)
		return block
	}
	var blockchain []*common.Block
	for seq := start; seq <= end; seq++ {
		block := newBlock(seq)
		block.Header.DataHash = protoutil.BlockDataHash(block.Data)
		blockchain = append(blockchain, block)
	}
	assignHashes(blockchain)
	return blockchain
}

func TestEndpointconfigFromConfigBlockGreenPath(t *testing.T) {
	t.Run("global endpoints", func(t *testing.T) {
		block, err := test.MakeGenesisBlock("mychannel")
		assert.NoError(t, err)

		cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
		assert.NoError(t, err)
		// For a block that doesn't have per org endpoints,
		// we take the global endpoints
		injectGlobalOrdererEndpoint(t, block, "globalEndpoint")
		endpointConfig, err := cluster.EndpointconfigFromConfigBlock(block, cryptoProvider)
		assert.NoError(t, err)
		assert.Len(t, endpointConfig, 1)
		assert.Equal(t, "globalEndpoint", endpointConfig[0].Endpoint)

		bl, _ := pem.Decode(endpointConfig[0].TLSRootCAs[0])
		cert, err := x509.ParseCertificate(bl.Bytes)
		assert.NoError(t, err)

		assert.True(t, cert.IsCA)
	})

	t.Run("per org endpoints", func(t *testing.T) {
		block, err := test.MakeGenesisBlock("mychannel")
		assert.NoError(t, err)

		// Make a second config.
		gConf := genesisconfig.Load(genesisconfig.SampleSingleMSPSoloProfile, configtest.GetDevConfigDir())
		gConf.Orderer.Capabilities = map[string]bool{
			capabilities.OrdererV2_0: true,
		}
		channelGroup, err := encoder.NewChannelGroup(gConf)
		assert.NoError(t, err)

		cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
		assert.NoError(t, err)
		bundle, err := channelconfig.NewBundle("mychannel", &common.Config{ChannelGroup: channelGroup}, cryptoProvider)
		assert.NoError(t, err)

		msps, err := bundle.MSPManager().GetMSPs()
		assert.NoError(t, err)
		caBytes := msps["SampleOrg"].GetTLSRootCerts()[0]

		assert.NoError(t, err)
		injectAdditionalTLSCAEndpointPair(t, block, "anotherEndpoint", caBytes, "fooOrg")
		endpointConfig, err := cluster.EndpointconfigFromConfigBlock(block, cryptoProvider)
		assert.NoError(t, err)
		// And ensure that the endpoints that are taken, are the per org ones.
		assert.Len(t, endpointConfig, 2)
		for _, endpoint := range endpointConfig {
			// If this is the original organization (and not the clone),
			// the TLS CA is 'caBytes' read from the second block.
			if endpoint.Endpoint == "anotherEndpoint" {
				assert.Len(t, endpoint.TLSRootCAs, 1)
				assert.Equal(t, caBytes, endpoint.TLSRootCAs[0])
				continue
			}
			// Else, our endpoints are from the original org, and the TLS CA is something else.
			assert.NotEqual(t, caBytes, endpoint.TLSRootCAs[0])
			// The endpoints we expect to see are something else.
			assert.Equal(t, 0, strings.Index(endpoint.Endpoint, "127.0.0.1:"))
			bl, _ := pem.Decode(endpoint.TLSRootCAs[0])
			cert, err := x509.ParseCertificate(bl.Bytes)
			assert.NoError(t, err)
			assert.True(t, cert.IsCA)
		}
	})
}

func TestEndpointconfigFromConfigBlockFailures(t *testing.T) {
	cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
	assert.NoError(t, err)

	t.Run("nil block", func(t *testing.T) {
		certs, err := cluster.EndpointconfigFromConfigBlock(nil, cryptoProvider)
		assert.Nil(t, certs)
		assert.EqualError(t, err, "nil block")
	})

	t.Run("nil block data", func(t *testing.T) {
		certs, err := cluster.EndpointconfigFromConfigBlock(&common.Block{}, cryptoProvider)
		assert.Nil(t, certs)
		assert.EqualError(t, err, "block data is nil")
	})

	t.Run("no envelope", func(t *testing.T) {
		certs, err := cluster.EndpointconfigFromConfigBlock(&common.Block{
			Data: &common.BlockData{},
		}, cryptoProvider)
		assert.Nil(t, certs)
		assert.EqualError(t, err, "envelope index out of bounds")
	})

	t.Run("bad envelope", func(t *testing.T) {
		certs, err := cluster.EndpointconfigFromConfigBlock(&common.Block{
			Data: &common.BlockData{
				Data: [][]byte{{}},
			},
		}, cryptoProvider)
		assert.Nil(t, certs)
		assert.EqualError(t, err, "failed extracting bundle from envelope: envelope header cannot be nil")
	})
}

func TestConfigFromBlockBadInput(t *testing.T) {
	for _, testCase := range []struct {
		name          string
		block         *common.Block
		expectedError string
	}{
		{
			name:          "nil block",
			expectedError: "empty block",
			block:         nil,
		},
		{
			name:          "nil block data",
			expectedError: "empty block",
			block:         &common.Block{},
		},
		{
			name:          "no data in block",
			expectedError: "empty block",
			block:         &common.Block{Data: &common.BlockData{}},
		},
		{
			name:          "invalid payload",
			expectedError: "error unmarshaling Envelope: proto: common.Envelope: illegal tag 0 (wire type 1)",
			block:         &common.Block{Data: &common.BlockData{Data: [][]byte{{1, 2, 3}}}},
		},
		{
			name:          "bad genesis block",
			expectedError: "invalid config envelope: proto: common.ConfigEnvelope: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Header: &common.BlockHeader{}, Data: &common.BlockData{Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
					Payload: protoutil.MarshalOrPanic(&common.Payload{
						Data: []byte{1, 2, 3},
					}),
				})}},
			},
		},
		{
			name:          "invalid envelope in block",
			expectedError: "error unmarshaling Envelope: proto: common.Envelope: illegal tag 0 (wire type 1)",
			block:         &common.Block{Data: &common.BlockData{Data: [][]byte{{1, 2, 3}}}},
		},
		{
			name:          "invalid payload in block envelope",
			expectedError: "error unmarshaling Payload: proto: common.Payload: illegal tag 0 (wire type 1)",
			block: &common.Block{Data: &common.BlockData{Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
				Payload: []byte{1, 2, 3},
			})}}},
		},
		{
			name:          "invalid channel header",
			expectedError: "error unmarshaling ChannelHeader: proto: common.ChannelHeader: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Header: &common.BlockHeader{Number: 1},
				Data: &common.BlockData{Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
					Payload: protoutil.MarshalOrPanic(&common.Payload{
						Header: &common.Header{
							ChannelHeader: []byte{1, 2, 3},
						},
					}),
				})}},
			},
		},
		{
			name:          "invalid config block",
			expectedError: "invalid config envelope: proto: common.ConfigEnvelope: illegal tag 0 (wire type 1)",
			block: &common.Block{
				Header: &common.BlockHeader{},
				Data: &common.BlockData{Data: [][]byte{protoutil.MarshalOrPanic(&common.Envelope{
					Payload: protoutil.MarshalOrPanic(&common.Payload{
						Data: []byte{1, 2, 3},
						Header: &common.Header{
							ChannelHeader: protoutil.MarshalOrPanic(&common.ChannelHeader{
								Type: int32(common.HeaderType_CONFIG),
							}),
						},
					}),
				})}},
			},
		},
	} {
		t.Run(testCase.name, func(t *testing.T) {
			conf, err := cluster.ConfigFromBlock(testCase.block)
			assert.Nil(t, conf)
			assert.EqualError(t, err, testCase.expectedError)
		})
	}
}

func TestBlockValidationPolicyVerifier(t *testing.T) {
	config := genesisconfig.Load(genesisconfig.SampleInsecureSoloProfile, configtest.GetDevConfigDir())
	group, err := encoder.NewChannelGroup(config)
	assert.NoError(t, err)
	assert.NotNil(t, group)
	cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
	assert.NoError(t, err)

	validConfigEnvelope := &common.ConfigEnvelope{
		Config: &common.Config{
			ChannelGroup: group,
		},
	}

	for _, testCase := range []struct {
		description   string
		expectedError string
		envelope      *common.ConfigEnvelope
		policyMap     map[string]policies.Policy
		policy        policies.Policy
	}{
		/**
		{
			description:   "policy not found",
			expectedError: "policy /Channel/Orderer/BlockValidation wasn't found",
		},
		*/
		{
			description:   "policy evaluation fails",
			expectedError: "invalid signature",
			policy: &mocks.Policy{
				EvaluateSignedDataStub: func([]*protoutil.SignedData) error {
					return errors.New("invalid signature")
				},
			},
		},
		{
			description:   "bad config envelope",
			expectedError: "config must contain a channel group",
			policy:        &mocks.Policy{},
			envelope:      &common.ConfigEnvelope{Config: &common.Config{}},
		},
		{
			description: "good config envelope overrides custom policy manager",
			policy: &mocks.Policy{
				EvaluateSignedDataStub: func([]*protoutil.SignedData) error {
					return errors.New("invalid signature")
				},
			},
			envelope: validConfigEnvelope,
		},
	} {
		t.Run(testCase.description, func(t *testing.T) {
			mockPolicyManager := &mocks.PolicyManager{}
			if testCase.policy != nil {
				mockPolicyManager.GetPolicyReturns(testCase.policy, true)
			} else {
				mockPolicyManager.GetPolicyReturns(nil, false)
			}
			mockPolicyManager.GetPolicyReturns(testCase.policy, true)
			verifier := &cluster.BlockValidationPolicyVerifier{
				Logger:    flogging.MustGetLogger("test"),
				Channel:   "mychannel",
				PolicyMgr: mockPolicyManager,
				BCCSP:     cryptoProvider,
			}

			err := verifier.VerifyBlockSignature(nil, testCase.envelope)
			if testCase.expectedError != "" {
				assert.EqualError(t, err, testCase.expectedError)
			} else {
				assert.NoError(t, err)
			}
		})
	}
}

func TestBlockVerifierAssembler(t *testing.T) {
	config := genesisconfig.Load(genesisconfig.SampleInsecureSoloProfile, configtest.GetDevConfigDir())
	group, err := encoder.NewChannelGroup(config)
	assert.NoError(t, err)
	assert.NotNil(t, group)
	cryptoProvider, err := sw.NewDefaultSecurityLevelWithKeystore(sw.NewDummyKeyStore())
	assert.NoError(t, err)

	t.Run("Good config envelope", func(t *testing.T) {
		bva := &cluster.BlockVerifierAssembler{BCCSP: cryptoProvider}
		verifier, err := bva.VerifierFromConfig(&common.ConfigEnvelope{
			Config: &common.Config{
				ChannelGroup: group,
			},
		}, "mychannel")
		assert.NoError(t, err)

		assert.NoError(t, verifier.VerifyBlockSignature(nil, nil))
	})

	t.Run("Bad config envelope", func(t *testing.T) {
		bva := &cluster.BlockVerifierAssembler{BCCSP: cryptoProvider}
		_, err := bva.VerifierFromConfig(&common.ConfigEnvelope{}, "mychannel")
		assert.EqualError(t, err, "failed extracting bundle from envelope: channelconfig Config cannot be nil")
	})
}

func TestLastConfigBlock(t *testing.T) {
	blockRetriever := &mocks.BlockRetriever{}
	blockRetriever.On("Block", uint64(42)).Return(&common.Block{})
	blockRetriever.On("Block", uint64(666)).Return(nil)

	for _, testCase := range []struct {
		name           string
		block          *common.Block
		blockRetriever cluster.BlockRetriever
		expectedError  string
	}{
		{
			name:           "nil block",
			expectedError:  "nil block",
			blockRetriever: blockRetriever,
		},
		{
			name:          "nil support",
			expectedError: "nil blockRetriever",
			block:         &common.Block{},
		},
		{
			name:           "nil metadata",
			expectedError:  "failed to retrieve metadata: no metadata in block",
			blockRetriever: blockRetriever,
			block:          &common.Block{},
		},
		{
			name: "no block with index",
			block: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{{}, protoutil.MarshalOrPanic(&common.Metadata{
						Value: protoutil.MarshalOrPanic(&common.LastConfig{Index: 666}),
					})},
				},
			},
			expectedError:  "unable to retrieve last config block [666]",
			blockRetriever: blockRetriever,
		},
		{
			name: "valid last config block",
			block: &common.Block{
				Metadata: &common.BlockMetadata{
					Metadata: [][]byte{{}, protoutil.MarshalOrPanic(&common.Metadata{
						Value: protoutil.MarshalOrPanic(&common.LastConfig{Index: 42}),
					})},
				},
			},
			blockRetriever: blockRetriever,
		},
	} {
		testCase := testCase
		t.Run(testCase.name, func(t *testing.T) {
			block, err := cluster.LastConfigBlock(testCase.block, testCase.blockRetriever)
			if testCase.expectedError == "" {
				assert.NoError(t, err)
				assert.NotNil(t, block)
				return
			}
			assert.EqualError(t, err, testCase.expectedError)
			assert.Nil(t, block)
		})
	}
}

func TestVerificationRegistryRegisterVerifier(t *testing.T) {
	blockBytes, err := ioutil.ReadFile("testdata/mychannel.block")
	assert.NoError(t, err)

	block := &common.Block{}
	assert.NoError(t, proto.Unmarshal(blockBytes, block))

	verifier := &mocks.BlockVerifier{}

	verifierFactory := &mocks.VerifierFactory{}
	verifierFactory.On("VerifierFromConfig",
		mock.Anything, "mychannel").Return(verifier, nil)

	registry := &cluster.VerificationRegistry{
		Logger:             flogging.MustGetLogger("test"),
		VerifiersByChannel: make(map[string]cluster.BlockVerifier),
		VerifierFactory:    verifierFactory,
	}

	var loadCount int
	registry.LoadVerifier = func(chain string) cluster.BlockVerifier {
		assert.Equal(t, "mychannel", chain)
		loadCount++
		return verifier
	}

	v := registry.RetrieveVerifier("mychannel")
	assert.Nil(t, v)

	registry.RegisterVerifier("mychannel")
	v = registry.RetrieveVerifier("mychannel")
	assert.Equal(t, verifier, v)
	assert.Equal(t, 1, loadCount)

	// If the verifier exists, this is a no-op
	registry.RegisterVerifier("mychannel")
	assert.Equal(t, 1, loadCount)
}

func TestVerificationRegistry(t *testing.T) {
	blockBytes, err := ioutil.ReadFile("testdata/mychannel.block")
	assert.NoError(t, err)

	block := &common.Block{}
	assert.NoError(t, proto.Unmarshal(blockBytes, block))

	flogging.ActivateSpec("test=DEBUG")
	defer flogging.Reset()

	verifier := &mocks.BlockVerifier{}

	for _, testCase := range []struct {
		description           string
		verifiersByChannel    map[string]cluster.BlockVerifier
		blockCommitted        *common.Block
		channelCommitted      string
		channelRetrieved      string
		expectedVerifier      cluster.BlockVerifier
		verifierFromConfig    cluster.BlockVerifier
		verifierFromConfigErr error
		loggedMessages        map[string]struct{}
	}{
		{
			description:      "bad block",
			blockCommitted:   &common.Block{},
			channelRetrieved: "foo",
			channelCommitted: "foo",
			loggedMessages: map[string]struct{}{
				"Failed parsing block of channel foo: empty block, content: " +
					"{\n\t\"data\": null,\n\t\"header\": null,\n\t\"metadata\": null\n}\n": {},
				"No verifier for channel foo exists": {},
			},
			expectedVerifier: nil,
		},
		{
			description:      "not a config block",
			blockCommitted:   createBlockChain(5, 5)[0],
			channelRetrieved: "foo",
			channelCommitted: "foo",
			loggedMessages: map[string]struct{}{
				"No verifier for channel foo exists":                             {},
				"Committed block [5] for channel foo that is not a config block": {},
			},
			expectedVerifier: nil,
		},
		{
			description:           "valid block but verifier from config fails",
			blockCommitted:        block,
			verifierFromConfigErr: errors.New("invalid MSP config"),
			channelRetrieved:      "bar",
			channelCommitted:      "bar",
			loggedMessages: map[string]struct{}{
				"Failed creating a verifier from a " +
					"config block for channel bar: invalid MSP config, " +
					"content: " + cluster.BlockToString(block): {},
				"No verifier for channel bar exists": {},
			},
			expectedVerifier: nil,
		},
		{
			description:        "valid block and verifier from config succeeds but wrong channel retrieved",
			blockCommitted:     block,
			verifierFromConfig: verifier,
			channelRetrieved:   "foo",
			channelCommitted:   "bar",
			loggedMessages: map[string]struct{}{
				"No verifier for channel foo exists":         {},
				"Committed config block [0] for channel bar": {},
			},
			expectedVerifier:   nil,
			verifiersByChannel: make(map[string]cluster.BlockVerifier),
		},
		{
			description:        "valid block and verifier from config succeeds",
			blockCommitted:     block,
			verifierFromConfig: verifier,
			channelRetrieved:   "bar",
			channelCommitted:   "bar",
			loggedMessages: map[string]struct{}{
				"Committed config block [0] for channel bar": {},
			},
			expectedVerifier:   verifier,
			verifiersByChannel: make(map[string]cluster.BlockVerifier),
		},
	} {
		t.Run(testCase.description, func(t *testing.T) {
			verifierFactory := &mocks.VerifierFactory{}
			verifierFactory.On("VerifierFromConfig",
				mock.Anything, testCase.channelCommitted).Return(testCase.verifierFromConfig, testCase.verifierFromConfigErr)

			registry := &cluster.VerificationRegistry{
				Logger:             flogging.MustGetLogger("test"),
				VerifiersByChannel: testCase.verifiersByChannel,
				VerifierFactory:    verifierFactory,
			}

			loggedEntriesByMethods := make(map[string]struct{})
			// Configure the logger to collect the message logged
			registry.Logger = registry.Logger.WithOptions(zap.Hooks(func(entry zapcore.Entry) error {
				loggedEntriesByMethods[entry.Message] = struct{}{}
				return nil
			}))

			registry.BlockCommitted(testCase.blockCommitted, testCase.channelCommitted)
			verifier := registry.RetrieveVerifier(testCase.channelRetrieved)

			assert.Equal(t, testCase.loggedMessages, loggedEntriesByMethods)
			assert.Equal(t, testCase.expectedVerifier, verifier)
		})
	}
}

func TestLedgerInterceptor(t *testing.T) {
	block := &common.Block{}

	ledger := &mocks.LedgerWriter{}
	ledger.On("Append", block).Return(nil).Once()

	var intercepted bool

	var interceptedLedger cluster.LedgerWriter = &cluster.LedgerInterceptor{
		Channel:      "mychannel",
		LedgerWriter: ledger,
		InterceptBlockCommit: func(b *common.Block, channel string) {
			assert.Equal(t, block, b)
			assert.Equal(t, "mychannel", channel)
			intercepted = true
		},
	}

	err := interceptedLedger.Append(block)
	assert.NoError(t, err)
	assert.True(t, intercepted)
	ledger.AssertCalled(t, "Append", block)
}

func injectAdditionalTLSCAEndpointPair(t *testing.T, block *common.Block, endpoint string, tlsCA []byte, orgName string) {
	// Unwrap the layers until we reach the orderer addresses
	env, err := protoutil.ExtractEnvelope(block, 0)
	assert.NoError(t, err)
	payload, err := protoutil.UnmarshalPayload(env.Payload)
	assert.NoError(t, err)
	confEnv, err := configtx.UnmarshalConfigEnvelope(payload.Data)
	assert.NoError(t, err)
	ordererGrp := confEnv.Config.ChannelGroup.Groups[channelconfig.OrdererGroupKey].Groups
	// Get the first orderer org config
	var firstOrdererConfig *common.ConfigGroup
	for _, grp := range ordererGrp {
		firstOrdererConfig = grp
		break
	}
	// Duplicate it.
	secondOrdererConfig := proto.Clone(firstOrdererConfig).(*common.ConfigGroup)
	ordererGrp[orgName] = secondOrdererConfig
	// Reach the FabricMSPConfig buried in it.
	mspConfig := &msp.MSPConfig{}
	err = proto.Unmarshal(secondOrdererConfig.Values[channelconfig.MSPKey].Value, mspConfig)
	assert.NoError(t, err)

	fabricConfig := &msp.FabricMSPConfig{}
	err = proto.Unmarshal(mspConfig.Config, fabricConfig)
	assert.NoError(t, err)

	// Plant the given TLS CA in it.
	fabricConfig.TlsRootCerts = [][]byte{tlsCA}
	// No intermediate root CAs, to make the test simpler.
	fabricConfig.TlsIntermediateCerts = nil
	// Rename it.
	fabricConfig.Name = orgName

	// Pack the MSP config back into the config
	secondOrdererConfig.Values[channelconfig.MSPKey].Value = protoutil.MarshalOrPanic(&msp.MSPConfig{
		Config: protoutil.MarshalOrPanic(fabricConfig),
		Type:   mspConfig.Type,
	})

	// Inject the endpoint
	ordererOrgProtos := &common.OrdererAddresses{
		Addresses: []string{endpoint},
	}
	secondOrdererConfig.Values[channelconfig.EndpointsKey].Value = protoutil.MarshalOrPanic(ordererOrgProtos)

	// Fold everything back into the block
	payload.Data = protoutil.MarshalOrPanic(confEnv)
	env.Payload = protoutil.MarshalOrPanic(payload)
	block.Data.Data[0] = protoutil.MarshalOrPanic(env)
}

func TestEndpointCriteriaString(t *testing.T) {
	// The top cert is the issuer of the bottom cert
	certs := `-----BEGIN CERTIFICATE-----
MIIBozCCAUigAwIBAgIQMXmzUnikiAZDr4VsrBL+rzAKBggqhkjOPQQDAjAxMS8w
LQYDVQQFEyY2NTc2NDA3Njc5ODcwOTA3OTEwNDM5NzkxMTAwNzA0Mzk3Njg3OTAe
Fw0xOTExMTEyMDM5MDRaFw0yOTExMDkyMDM5MDRaMDExLzAtBgNVBAUTJjY1NzY0
MDc2Nzk4NzA5MDc5MTA0Mzk3OTExMDA3MDQzOTc2ODc5MFkwEwYHKoZIzj0CAQYI
KoZIzj0DAQcDQgAEzBBkRvWgasCKf1pejwpOu+1Fv9FffOZMHnna/7lfMrAqOs8d
HMDVU7mSexu7YNTpAwm4vkdHXi35H8zlVABTxaNCMEAwDgYDVR0PAQH/BAQDAgGm
MB0GA1UdJQQWMBQGCCsGAQUFBwMCBggrBgEFBQcDATAPBgNVHRMBAf8EBTADAQH/
MAoGCCqGSM49BAMCA0kAMEYCIQCXqXoYLAJN9diIdGxPlRQJgJLju4brWXZfyt3s
E9TjFwIhAOuUJjcOchdP6UA9WLnVWciEo1Omf59NgfHL1gUPb/t6
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIBpDCCAUqgAwIBAgIRAIyvtL0z1xQ+NecXeH1HmmAwCgYIKoZIzj0EAwIwMTEv
MC0GA1UEBRMmNjU3NjQwNzY3OTg3MDkwNzkxMDQzOTc5MTEwMDcwNDM5NzY4Nzkw
HhcNMTkxMTExMjAzOTA0WhcNMTkxMTEyMjAzOTA0WjAyMTAwLgYDVQQFEycxODcw
MDQyMzcxODQwMjY5Mzk2ODUxNzk1NzM3MzIyMTc2OTA3MjAwWTATBgcqhkjOPQIB
BggqhkjOPQMBBwNCAARZBFDBOfC7T9RbsX+PgyE6sM7ocuwn6krIGjc00ICivFgQ
qdHMU7hiswiYwSvwh9MDHlprCRW3ycSgEYQgKU5to0IwQDAOBgNVHQ8BAf8EBAMC
BaAwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMA8GA1UdEQQIMAaHBH8A
AAEwCgYIKoZIzj0EAwIDSAAwRQIhAK6G7qr/ClszCFP25gsflA31+7eoss5vi3o4
qz8bY+s6AiBvO0aOfE8M4ibjmRE4vSXo0+gkOIJKqZcmiRdnJSr8Xw==
-----END CERTIFICATE-----`

	epc := cluster.EndpointCriteria{
		Endpoint:   "orderer.example.com:7050",
		TLSRootCAs: [][]byte{[]byte(certs)},
	}

	actual := fmt.Sprint(epc)
	expected := `{"CAs":[{"Expired":false,"Issuer":"self","Subject":"SERIALNUMBER=65764076798709079104397911007043976879"},{"Expired":true,"Issuer":"SERIALNUMBER=65764076798709079104397911007043976879","Subject":"SERIALNUMBER=187004237184026939685179573732217690720"}],"Endpoint":"orderer.example.com:7050"}`
	assert.Equal(t, expected, actual)
}

func TestComparisonMemoizer(t *testing.T) {
	var invocations int

	m := &cluster.ComparisonMemoizer{
		MaxEntries: 5,
		F: func(a, b []byte) bool {
			invocations++
			return bytes.Equal(a, b)
		},
	}

	// Warm-up cache
	for i := 0; i < 5; i++ {
		notSame := m.Compare([]byte{byte(i)}, []byte{1, 2, 3})
		require.False(t, notSame)
		require.Equal(t, i+1, invocations)
	}

	// Ensure lookups are cached
	for i := 0; i < 5; i++ {
		notSame := m.Compare([]byte{byte(i)}, []byte{1, 2, 3})
		require.False(t, notSame)
		require.Equal(t, 5, invocations)
	}

	// Put a new entry which will cause a cache miss
	same := m.Compare([]byte{5}, []byte{5})
	require.True(t, same)
	require.Equal(t, 6, invocations)

	// Keep adding more and more elements to the cache and ensure it stays smaller than its size
	for i := 0; i < 20; i++ {
		odd := m.Compare([]byte{byte(1)}, []byte{byte(i % 2)})
		require.Equal(t, i%2 != 0, odd)
		require.LessOrEqual(t, m.Size(), int(m.MaxEntries))
	}
}
```

## `protoutil/blockutils.go` (changed lines (13, 14, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264))

```go
/*
Copyright IBM Corp. All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package protoutil

import (
	"bytes"
	"crypto/sha256"
	"encoding/asn1"
	"encoding/base64"
	"fmt"
	"math/big"

	"github.com/golang/protobuf/proto"
	cb "github.com/hyperledger/fabric-protos-go/common"
	"github.com/pkg/errors"
)

// NewBlock constructs a block with no data and no metadata.
func NewBlock(seqNum uint64, previousHash []byte) *cb.Block {
	block := &cb.Block{}
	block.Header = &cb.BlockHeader{}
	block.Header.Number = seqNum
	block.Header.PreviousHash = previousHash
	block.Header.DataHash = []byte{}
	block.Data = &cb.BlockData{}

	var metadataContents [][]byte
	for i := 0; i < len(cb.BlockMetadataIndex_name); i++ {
		metadataContents = append(metadataContents, []byte{})
	}
	block.Metadata = &cb.BlockMetadata{Metadata: metadataContents}

	return block
}

type asn1Header struct {
	Number       *big.Int
	PreviousHash []byte
	DataHash     []byte
}

func BlockHeaderBytes(b *cb.BlockHeader) []byte {
	asn1Header := asn1Header{
		PreviousHash: b.PreviousHash,
		DataHash:     b.DataHash,
		Number:       new(big.Int).SetUint64(b.Number),
	}
	result, err := asn1.Marshal(asn1Header)
	if err != nil {
		// Errors should only arise for types which cannot be encoded, since the
		// BlockHeader type is known a-priori to contain only encodable types, an
		// error here is fatal and should not be propagated
		panic(err)
	}
	return result
}

func BlockHeaderHash(b *cb.BlockHeader) []byte {
	sum := sha256.Sum256(BlockHeaderBytes(b))
	return sum[:]
}

func BlockDataHash(b *cb.BlockData) []byte {
	sum := sha256.Sum256(bytes.Join(b.Data, nil))
	return sum[:]
}

// GetChannelIDFromBlockBytes returns channel ID given byte array which represents
// the block
func GetChannelIDFromBlockBytes(bytes []byte) (string, error) {
	block, err := UnmarshalBlock(bytes)
	if err != nil {
		return "", err
	}

	return GetChannelIDFromBlock(block)
}

// GetChannelIDFromBlock returns channel ID in the block
func GetChannelIDFromBlock(block *cb.Block) (string, error) {
	if block == nil || block.Data == nil || block.Data.Data == nil || len(block.Data.Data) == 0 {
		return "", errors.New("failed to retrieve channel id - block is empty")
	}
	var err error
	envelope, err := GetEnvelopeFromBlock(block.Data.Data[0])
	if err != nil {
		return "", err
	}
	payload, err := UnmarshalPayload(envelope.Payload)
	if err != nil {
		return "", err
	}

	if payload.Header == nil {
		return "", errors.New("failed to retrieve channel id - payload header is empty")
	}
	chdr, err := UnmarshalChannelHeader(payload.Header.ChannelHeader)
	if err != nil {
		return "", err
	}

	return chdr.ChannelId, nil
}

// GetMetadataFromBlock retrieves metadata at the specified index.
func GetMetadataFromBlock(block *cb.Block, index cb.BlockMetadataIndex) (*cb.Metadata, error) {
	if block.Metadata == nil {
		return nil, errors.New("no metadata in block")
	}

	if len(block.Metadata.Metadata) <= int(index) {
		return nil, errors.Errorf("no metadata at index [%s]", index)
	}

	md := &cb.Metadata{}
	err := proto.Unmarshal(block.Metadata.Metadata[index], md)
	if err != nil {
		return nil, errors.Wrapf(err, "error unmarshaling metadata at index [%s]", index)
	}
	return md, nil
}

// GetMetadataFromBlockOrPanic retrieves metadata at the specified index, or
// panics on error
func GetMetadataFromBlockOrPanic(block *cb.Block, index cb.BlockMetadataIndex) *cb.Metadata {
	md, err := GetMetadataFromBlock(block, index)
	if err != nil {
		panic(err)
	}
	return md
}

// GetConsenterMetadataFromBlock attempts to retrieve consenter metadata from the value
// stored in block metadata at index SIGNATURES (first field). If no consenter metadata
// is found there, it falls back to index ORDERER (third field).
func GetConsenterMetadataFromBlock(block *cb.Block) (*cb.Metadata, error) {
	m, err := GetMetadataFromBlock(block, cb.BlockMetadataIndex_SIGNATURES)
	if err != nil {
		return nil, errors.WithMessage(err, "failed to retrieve metadata")
	}

	// TODO FAB-15864 Remove this fallback when we can stop supporting upgrade from pre-1.4.1 orderer
	if len(m.Value) == 0 {
		return GetMetadataFromBlock(block, cb.BlockMetadataIndex_ORDERER)
	}

	obm := &cb.OrdererBlockMetadata{}
	err = proto.Unmarshal(m.Value, obm)
	if err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal orderer block metadata")
	}

	res := &cb.Metadata{}
	err = proto.Unmarshal(obm.ConsenterMetadata, res)
	if err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal consenter metadata")
	}

	return res, nil
}

// GetLastConfigIndexFromBlock retrieves the index of the last config block as
// encoded in the block metadata
func GetLastConfigIndexFromBlock(block *cb.Block) (uint64, error) {
	m, err := GetMetadataFromBlock(block, cb.BlockMetadataIndex_SIGNATURES)
	if err != nil {
		return 0, errors.WithMessage(err, "failed to retrieve metadata")
	}
	// TODO FAB-15864 Remove this fallback when we can stop supporting upgrade from pre-1.4.1 orderer
	if len(m.Value) == 0 {
		m, err := GetMetadataFromBlock(block, cb.BlockMetadataIndex_LAST_CONFIG)
		if err != nil {
			return 0, errors.WithMessage(err, "failed to retrieve metadata")
		}
		lc := &cb.LastConfig{}
		err = proto.Unmarshal(m.Value, lc)
		if err != nil {
			return 0, errors.Wrap(err, "error unmarshaling LastConfig")
		}
		return lc.Index, nil
	}

	obm := &cb.OrdererBlockMetadata{}
	err = proto.Unmarshal(m.Value, obm)
	if err != nil {
		return 0, errors.Wrap(err, "failed to unmarshal orderer block metadata")
	}
	return obm.LastConfig.Index, nil
}

// GetLastConfigIndexFromBlockOrPanic retrieves the index of the last config
// block as encoded in the block metadata, or panics on error
func GetLastConfigIndexFromBlockOrPanic(block *cb.Block) uint64 {
	index, err := GetLastConfigIndexFromBlock(block)
	if err != nil {
		panic(err)
	}
	return index
}

// CopyBlockMetadata copies metadata from one block into another
func CopyBlockMetadata(src *cb.Block, dst *cb.Block) {
	dst.Metadata = src.Metadata
	// Once copied initialize with rest of the
	// required metadata positions.
	InitBlockMetadata(dst)
}

// InitBlockMetadata initializes metadata structure
func InitBlockMetadata(block *cb.Block) {
	if block.Metadata == nil {
		block.Metadata = &cb.BlockMetadata{Metadata: [][]byte{{}, {}, {}, {}, {}}}
	} else if len(block.Metadata.Metadata) < int(cb.BlockMetadataIndex_COMMIT_HASH+1) {
		for i := int(len(block.Metadata.Metadata)); i <= int(cb.BlockMetadataIndex_COMMIT_HASH); i++ {
			block.Metadata.Metadata = append(block.Metadata.Metadata, []byte{})
		}
	}
}

func VerifyTransactionsAreWellFormed(block *cb.Block) error {
	if block == nil || block.Data == nil || len(block.Data.Data) == 0 {
		return fmt.Errorf("empty block")
	}

	// If we have a single transaction, and the block is a config block, then no need to check
	// well formed-ness, because there cannot be another transaction in the original block.
	if IsConfigBlock(block) {
		return nil
	}

	for i, rawTx := range block.Data.Data {
		env := &cb.Envelope{}
		if err := proto.Unmarshal(rawTx, env); err != nil {
			return fmt.Errorf("transaction %d is invalid: %v", i, err)
		}

		if len(env.Payload) == 0 {
			return fmt.Errorf("transaction %d has no payload", i)
		}

		if len(env.Signature) == 0 {
			return fmt.Errorf("transaction %d has no signature", i)
		}

		expected, err := proto.Marshal(env)
		if err != nil {
			return fmt.Errorf("failed re-marshaling envelope: %v", err)
		}

		if len(expected) < len(rawTx) {
			return fmt.Errorf("transaction %d has %d trailing bytes", i, len(rawTx)-len(expected))
		}
		if !bytes.Equal(expected, rawTx) {
			return fmt.Errorf("transaction %d (%s) does not match its raw form (%s)", i,
				base64.StdEncoding.EncodeToString(expected), base64.StdEncoding.EncodeToString(rawTx))
		}
	}

	return nil
}
```

## `protoutil/blockutils_test.go` (changed lines (213, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506))

```go
/*
Copyright IBM Corp. All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package protoutil_test

import (
	"crypto/sha256"
	"encoding/asn1"
	"math"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/hyperledger/fabric-protos-go/common"
	cb "github.com/hyperledger/fabric-protos-go/common"
	configtxtest "github.com/hyperledger/fabric/common/configtx/test"
	"github.com/hyperledger/fabric/protoutil"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

var testChannelID = "myuniquetestchainid"

func TestNewBlock(t *testing.T) {
	var block *cb.Block
	assert.Nil(t, block.GetHeader())
	assert.Nil(t, block.GetData())
	assert.Nil(t, block.GetMetadata())

	data := &cb.BlockData{
		Data: [][]byte{{0, 1, 2}},
	}
	block = protoutil.NewBlock(uint64(0), []byte("datahash"))
	assert.Equal(t, []byte("datahash"), block.Header.PreviousHash, "Incorrect previous hash")
	assert.NotNil(t, block.GetData())
	assert.NotNil(t, block.GetMetadata())
	block.GetHeader().DataHash = protoutil.BlockDataHash(data)

	asn1Bytes, err := asn1.Marshal(struct {
		Number       int64
		PreviousHash []byte
		DataHash     []byte
	}{
		Number:       0,
		DataHash:     protoutil.BlockDataHash(data),
		PreviousHash: []byte("datahash"),
	})
	headerHash := sha256.Sum256(asn1Bytes)
	assert.NoError(t, err)
	assert.Equal(t, asn1Bytes, protoutil.BlockHeaderBytes(block.Header), "Incorrect marshaled blockheader bytes")
	assert.Equal(t, headerHash[:], protoutil.BlockHeaderHash(block.Header), "Incorrect blockheader hash")
}

func TestGoodBlockHeaderBytes(t *testing.T) {
	goodBlockHeader := &common.BlockHeader{
		Number:       1,
		PreviousHash: []byte("foo"),
		DataHash:     []byte("bar"),
	}

	_ = protoutil.BlockHeaderBytes(goodBlockHeader) // Should not panic

	goodBlockHeaderMaxNumber := &common.BlockHeader{
		Number:       math.MaxUint64,
		PreviousHash: []byte("foo"),
		DataHash:     []byte("bar"),
	}

	_ = protoutil.BlockHeaderBytes(goodBlockHeaderMaxNumber) // Should not panic
}

func TestGetChannelIDFromBlockBytes(t *testing.T) {
	gb, err := configtxtest.MakeGenesisBlock(testChannelID)
	assert.NoError(t, err, "Failed to create test configuration block")
	bytes, err := proto.Marshal(gb)
	assert.NoError(t, err)
	cid, err := protoutil.GetChannelIDFromBlockBytes(bytes)
	assert.NoError(t, err)
	assert.Equal(t, testChannelID, cid, "Failed to return expected chain ID")

	// bad block bytes
	_, err = protoutil.GetChannelIDFromBlockBytes([]byte("bad block"))
	assert.Error(t, err, "Expected error with malformed block bytes")
}

func TestGetChannelIDFromBlock(t *testing.T) {
	var err error
	var gb *common.Block
	var cid string

	// nil block
	_, err = protoutil.GetChannelIDFromBlock(gb)
	assert.Error(t, err, "Expected error getting channel id from nil block")

	gb, err = configtxtest.MakeGenesisBlock(testChannelID)
	assert.NoError(t, err, "Failed to create test configuration block")

	cid, err = protoutil.GetChannelIDFromBlock(gb)
	assert.NoError(t, err, "Failed to get chain ID from block")
	assert.Equal(t, testChannelID, cid, "Failed to return expected chain ID")

	// missing data
	badBlock := gb
	badBlock.Data = nil
	_, err = protoutil.GetChannelIDFromBlock(badBlock)
	assert.Error(t, err, "Expected error with missing block data")

	// no envelope
	badBlock = &cb.Block{
		Data: &cb.BlockData{
			Data: [][]byte{[]byte("bad envelope")},
		},
	}
	_, err = protoutil.GetChannelIDFromBlock(badBlock)
	assert.Error(t, err, "Expected error with no envelope in data")

	// bad payload
	env, _ := proto.Marshal(&cb.Envelope{
		Payload: []byte("bad payload"),
	})
	badBlock = &cb.Block{
		Data: &cb.BlockData{
			Data: [][]byte{env},
		},
	}
	_, err = protoutil.GetChannelIDFromBlock(badBlock)
	assert.Error(t, err, "Expected error - malformed payload")

	// bad channel header
	payload, _ := proto.Marshal(&cb.Payload{
		Header: &cb.Header{
			ChannelHeader: []byte("bad header"),
		},
	})
	env, _ = proto.Marshal(&cb.Envelope{
		Payload: payload,
	})
	badBlock = &cb.Block{
		Data: &cb.BlockData{
			Data: [][]byte{env},
		},
	}
	_, err = protoutil.GetChannelIDFromBlock(badBlock)
	assert.Error(t, err, "Expected error with malformed channel header")

	// nil payload header
	payload, _ = proto.Marshal(&cb.Payload{})
	env, _ = proto.Marshal(&cb.Envelope{
		Payload: payload,
	})
	badBlock = &cb.Block{
		Data: &cb.BlockData{
			Data: [][]byte{env},
		},
	}
	_, err = protoutil.GetChannelIDFromBlock(badBlock)
	assert.Error(t, err, "Expected error when payload header is nil")
}

func TestGetBlockFromBlockBytes(t *testing.T) {
	testChainID := "myuniquetestchainid"
	gb, err := configtxtest.MakeGenesisBlock(testChainID)
	assert.NoError(t, err, "Failed to create test configuration block")
	blockBytes, err := protoutil.Marshal(gb)
	assert.NoError(t, err, "Failed to marshal block")
	_, err = protoutil.UnmarshalBlock(blockBytes)
	assert.NoError(t, err, "to get block from block bytes")

	// bad block bytes
	_, err = protoutil.UnmarshalBlock([]byte("bad block"))
	assert.Error(t, err, "Expected error for malformed block bytes")
}

func TestGetMetadataFromBlock(t *testing.T) {
	t.Run("new block", func(t *testing.T) {
		block := protoutil.NewBlock(0, nil)
		md, err := protoutil.GetMetadataFromBlock(block, cb.BlockMetadataIndex_ORDERER)
		assert.NoError(t, err, "Unexpected error extracting metadata from new block")
		assert.Nil(t, md.Value, "Expected metadata field value to be nil")
		assert.Equal(t, 0, len(md.Value), "Expected length of metadata field value to be 0")
		md = protoutil.GetMetadataFromBlockOrPanic(block, cb.BlockMetadataIndex_ORDERER)
		assert.NotNil(t, md, "Expected to get metadata from block")
	})
	t.Run("no metadata", func(t *testing.T) {
		block := protoutil.NewBlock(0, nil)
		block.Metadata = nil
		_, err := protoutil.GetMetadataFromBlock(block, cb.BlockMetadataIndex_ORDERER)
		assert.Error(t, err, "Expected error with nil metadata")
		assert.Contains(t, err.Error(), "no metadata in block")
	})
	t.Run("no metadata at index", func(t *testing.T) {
		block := protoutil.NewBlock(0, nil)
		block.Metadata.Metadata = [][]byte{{1, 2, 3}}
		_, err := protoutil.GetMetadataFromBlock(block, cb.BlockMetadataIndex_LAST_CONFIG)
		assert.Error(t, err, "Expected error with nil metadata")
		assert.Contains(t, err.Error(), "no metadata at index")
	})
	t.Run("malformed metadata", func(t *testing.T) {
		block := protoutil.NewBlock(0, nil)
		block.Metadata.Metadata[cb.BlockMetadataIndex_ORDERER] = []byte("bad metadata")
		_, err := protoutil.GetMetadataFromBlock(block, cb.BlockMetadataIndex_ORDERER)
		assert.Error(t, err, "Expected error with malformed metadata")
		assert.Contains(t, err.Error(), "error unmarshaling metadata at index [ORDERER]")
		assert.Panics(t, func() {
			_ = protoutil.GetMetadataFromBlockOrPanic(block, cb.BlockMetadataIndex_ORDERER)
		}, "Expected panic with malformed metadata")
	})
}

func TestGetConsenterMetadataFromBlock(t *testing.T) {
	cases := []struct {
		name       string
		value      []byte
		signatures []byte
		orderer    []byte
		pass       bool
	}{
		{
			name:       "empty",
			value:      nil,
			signatures: nil,
			orderer:    nil,
			pass:       true,
		},
		{
			name:  "signature only",
			value: []byte("hello"),
			signatures: protoutil.MarshalOrPanic(&cb.Metadata{
				Value: protoutil.MarshalOrPanic(&cb.OrdererBlockMetadata{
					ConsenterMetadata: protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("hello")}),
				}),
			}),
			orderer: nil,
			pass:    true,
		},
		{
			name:       "orderer only",
			value:      []byte("hello"),
			signatures: nil,
			orderer:    protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("hello")}),
			pass:       true,
		},
		{
			name:  "both signatures and orderer",
			value: []byte("hello"),
			signatures: protoutil.MarshalOrPanic(&cb.Metadata{
				Value: protoutil.MarshalOrPanic(&cb.OrdererBlockMetadata{
					ConsenterMetadata: protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("hello")}),
				}),
			}),
			orderer: protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("hello")}),
			pass:    true,
		},
		{
			name:       "malformed OrdererBlockMetadata",
			signatures: protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("malformed")}),
			orderer:    nil,
			pass:       false,
		},
	}

	for _, test := range cases {
		block := protoutil.NewBlock(0, nil)
		block.Metadata.Metadata[cb.BlockMetadataIndex_SIGNATURES] = test.signatures
		block.Metadata.Metadata[cb.BlockMetadataIndex_ORDERER] = test.orderer
		result, err := protoutil.GetConsenterMetadataFromBlock(block)

		if test.pass {
			require.NoError(t, err)
			require.Equal(t, result.Value, test.value)
		} else {
			require.Error(t, err)
		}
	}
}

func TestInitBlockMeta(t *testing.T) {
	// block with no metadata
	block := &cb.Block{}
	protoutil.InitBlockMetadata(block)
	// should have 3 entries
	assert.Equal(t, 5, len(block.Metadata.Metadata), "Expected block to have 5 metadata entries")

	// block with a single entry
	block = &cb.Block{
		Metadata: &cb.BlockMetadata{},
	}
	block.Metadata.Metadata = append(block.Metadata.Metadata, []byte{})
	protoutil.InitBlockMetadata(block)
	// should have 3 entries
	assert.Equal(t, 5, len(block.Metadata.Metadata), "Expected block to have 5 metadata entries")
}

func TestCopyBlockMetadata(t *testing.T) {
	srcBlock := protoutil.NewBlock(0, nil)
	dstBlock := &cb.Block{}

	metadata, _ := proto.Marshal(&cb.Metadata{
		Value: []byte("orderer metadata"),
	})
	srcBlock.Metadata.Metadata[cb.BlockMetadataIndex_ORDERER] = metadata
	protoutil.CopyBlockMetadata(srcBlock, dstBlock)

	// check that the copy worked
	assert.Equal(t, len(srcBlock.Metadata.Metadata), len(dstBlock.Metadata.Metadata),
		"Expected target block to have same number of metadata entries after copy")
	assert.Equal(t, metadata, dstBlock.Metadata.Metadata[cb.BlockMetadataIndex_ORDERER],
		"Unexpected metadata from target block")
}

func TestGetLastConfigIndexFromBlock(t *testing.T) {
	index := uint64(2)
	block := protoutil.NewBlock(0, nil)

	t.Run("block with last config metadata in signatures field", func(t *testing.T) {
		block.Metadata.Metadata[cb.BlockMetadataIndex_SIGNATURES] = protoutil.MarshalOrPanic(&cb.Metadata{
			Value: protoutil.MarshalOrPanic(&cb.OrdererBlockMetadata{
				LastConfig: &cb.LastConfig{Index: 2},
			}),
		})
		result, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.NoError(t, err, "Unexpected error returning last config index")
		assert.Equal(t, index, result, "Unexpected last config index returned from block")
		result = protoutil.GetLastConfigIndexFromBlockOrPanic(block)
		assert.Equal(t, index, result, "Unexpected last config index returned from block")
	})

	t.Run("block with malformed signatures", func(t *testing.T) {
		block.Metadata.Metadata[cb.BlockMetadataIndex_SIGNATURES] = []byte("apple")
		_, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.Error(t, err)
		assert.Contains(t, err.Error(), "failed to retrieve metadata: error unmarshaling metadata at index [SIGNATURES]")
	})

	t.Run("block with malformed orderer block metadata", func(t *testing.T) {
		block.Metadata.Metadata[cb.BlockMetadataIndex_SIGNATURES] = protoutil.MarshalOrPanic(&cb.Metadata{Value: []byte("banana")})
		_, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.Error(t, err)
		assert.Contains(t, err.Error(), "failed to unmarshal orderer block metadata")
	})

	// TODO: FAB-15864 remove the tests below when we stop supporting upgrade from
	//       pre-1.4.1 orderer
	t.Run("block with deprecated (pre-1.4.1) last config", func(t *testing.T) {
		block = protoutil.NewBlock(0, nil)
		block.Metadata.Metadata[cb.BlockMetadataIndex_LAST_CONFIG] = protoutil.MarshalOrPanic(&cb.Metadata{
			Value: protoutil.MarshalOrPanic(&cb.LastConfig{
				Index: index,
			}),
		})
		result, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.NoError(t, err, "Unexpected error returning last config index")
		assert.Equal(t, index, result, "Unexpected last config index returned from block")
		result = protoutil.GetLastConfigIndexFromBlockOrPanic(block)
		assert.Equal(t, index, result, "Unexpected last config index returned from block")
	})

	t.Run("malformed metadata", func(t *testing.T) {
		block.Metadata.Metadata[cb.BlockMetadataIndex_LAST_CONFIG] = []byte("bad metadata")
		_, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.Error(t, err)
		assert.Contains(t, err.Error(), "failed to retrieve metadata: error unmarshaling metadata at index [LAST_CONFIG]")
	})

	t.Run("malformed last config", func(t *testing.T) {
		block.Metadata.Metadata[cb.BlockMetadataIndex_LAST_CONFIG] = protoutil.MarshalOrPanic(&cb.Metadata{
			Value: []byte("bad last config"),
		})
		_, err := protoutil.GetLastConfigIndexFromBlock(block)
		assert.Error(t, err, "Expected error with malformed last config metadata")
		assert.Contains(t, err.Error(), "error unmarshaling LastConfig")
		assert.Panics(t, func() {
			_ = protoutil.GetLastConfigIndexFromBlockOrPanic(block)
		}, "Expected panic with malformed last config metadata")
	})
}

func TestVerifyTransactionsAreWellFormed(t *testing.T) {
	originalBlock := &cb.Block{
		Data: &cb.BlockData{
			Data: [][]byte{
				marshalOrPanic(&cb.Envelope{
					Payload:   []byte{1, 2, 3},
					Signature: []byte{4, 5, 6},
				}),
				marshalOrPanic(&cb.Envelope{
					Payload:   []byte{7, 8, 9},
					Signature: []byte{10, 11, 12},
				}),
			},
		},
	}

	forgedBlock := proto.Clone(originalBlock).(*cb.Block)
	tmp := make([]byte, len(forgedBlock.Data.Data[0])+len(forgedBlock.Data.Data[1]))
	copy(tmp, forgedBlock.Data.Data[0])
	copy(tmp[len(forgedBlock.Data.Data[0]):], forgedBlock.Data.Data[1])
	forgedBlock.Data.Data = [][]byte{tmp} // Replace transactions {0,1} with transaction {0 || 1}

	for _, tst := range []struct {
		name          string
		expectedError string
		block         *cb.Block
	}{
		{
			name: "config block",
			block: &cb.Block{Data: &cb.BlockData{
				Data: [][]byte{
					protoutil.MarshalOrPanic(
						&cb.Envelope{
							Payload: protoutil.MarshalOrPanic(&cb.Payload{
								Header: &cb.Header{
									ChannelHeader: protoutil.MarshalOrPanic(&cb.ChannelHeader{
										Type: int32(cb.HeaderType_CONFIG),
									}),
								},
							}),
						}),
				},
			}},
		},
		{
			name:          "empty block",
			expectedError: "empty block",
		},
		{
			name:          "no block data",
			block:         &cb.Block{},
			expectedError: "empty block",
		},
		{
			name:          "no transactions",
			block:         &cb.Block{Data: &cb.BlockData{}},
			expectedError: "empty block",
		},
		{
			name: "single transaction",
			block: &cb.Block{Data: &cb.BlockData{Data: [][]byte{marshalOrPanic(&cb.Envelope{
				Payload:   []byte{1, 2, 3},
				Signature: []byte{4, 5, 6},
			})}}},
		},

		{
			name:  "good block",
			block: originalBlock,
		},
		{
			name:          "forged block",
			block:         forgedBlock,
			expectedError: "transaction 0 has 10 trailing bytes",
		},
		{
			name:          "no signature",
			expectedError: "transaction 0 has no signature",
			block: &cb.Block{
				Data: &cb.BlockData{
					Data: [][]byte{
						marshalOrPanic(&cb.Envelope{
							Payload: []byte{1, 2, 3},
						}),
					},
				},
			},
		},
		{
			name:          "no payload",
			expectedError: "transaction 0 has no payload",
			block: &cb.Block{
				Data: &cb.BlockData{
					Data: [][]byte{
						marshalOrPanic(&cb.Envelope{
							Signature: []byte{4, 5, 6},
						}),
					},
				},
			},
		},
		{
			name:          "transaction invalid",
			expectedError: "transaction 0 is invalid: proto: common.Envelope: illegal tag 0 (wire type 6)",
			block: &cb.Block{
				Data: &cb.BlockData{
					Data: [][]byte{
						marshalOrPanic(&cb.Envelope{
							Payload:   []byte{1, 2, 3},
							Signature: []byte{4, 5, 6},
						})[9:],
					},
				},
			},
		},
	} {
		tst := tst
		t.Run(tst.name, func(t *testing.T) {
			err := protoutil.VerifyTransactionsAreWellFormed(tst.block)
			if tst.expectedError == "" {
				require.NoError(t, err)
			} else {
				require.Contains(t, err.Error(), tst.expectedError)
			}
		})
	}
}
```

## `protoutil/commonutils.go` (changed lines (191, 192, 193, 194, 195, 196, 197, 198, 199, 200))

```go
/*
Copyright IBM Corp. All Rights Reserved.

SPDX-License-Identifier: Apache-2.0
*/

package protoutil

import (
	"crypto/rand"
	"fmt"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/golang/protobuf/ptypes/timestamp"
	cb "github.com/hyperledger/fabric-protos-go/common"
	"github.com/hyperledger/fabric/internal/pkg/identity"
	"github.com/pkg/errors"
)

// MarshalOrPanic serializes a protobuf message and panics if this
// operation fails
func MarshalOrPanic(pb proto.Message) []byte {
	data, err := proto.Marshal(pb)
	if err != nil {
		panic(err)
	}
	return data
}

// Marshal serializes a protobuf message.
func Marshal(pb proto.Message) ([]byte, error) {
	return proto.Marshal(pb)
}

// CreateNonceOrPanic generates a nonce using the common/crypto package
// and panics if this operation fails.
func CreateNonceOrPanic() []byte {
	nonce, err := CreateNonce()
	if err != nil {
		panic(err)
	}
	return nonce
}

// CreateNonce generates a nonce using the common/crypto package.
func CreateNonce() ([]byte, error) {
	nonce, err := getRandomNonce()
	return nonce, errors.WithMessage(err, "error generating random nonce")
}

// UnmarshalEnvelopeOfType unmarshals an envelope of the specified type,
// including unmarshaling the payload data
func UnmarshalEnvelopeOfType(envelope *cb.Envelope, headerType cb.HeaderType, message proto.Message) (*cb.ChannelHeader, error) {
	payload, err := UnmarshalPayload(envelope.Payload)
	if err != nil {
		return nil, err
	}

	if payload.Header == nil {
		return nil, errors.New("envelope must have a Header")
	}

	chdr, err := UnmarshalChannelHeader(payload.Header.ChannelHeader)
	if err != nil {
		return nil, err
	}

	if chdr.Type != int32(headerType) {
		return nil, errors.Errorf("invalid type %s, expected %s", cb.HeaderType(chdr.Type), headerType)
	}

	err = proto.Unmarshal(payload.Data, message)
	err = errors.Wrapf(err, "error unmarshaling message for type %s", headerType)
	return chdr, err
}

// ExtractEnvelopeOrPanic retrieves the requested envelope from a given block
// and unmarshals it -- it panics if either of these operations fail
func ExtractEnvelopeOrPanic(block *cb.Block, index int) *cb.Envelope {
	envelope, err := ExtractEnvelope(block, index)
	if err != nil {
		panic(err)
	}
	return envelope
}

// ExtractEnvelope retrieves the requested envelope from a given block and
// unmarshals it
func ExtractEnvelope(block *cb.Block, index int) (*cb.Envelope, error) {
	if block.Data == nil {
		return nil, errors.New("block data is nil")
	}

	envelopeCount := len(block.Data.Data)
	if index < 0 || index >= envelopeCount {
		return nil, errors.New("envelope index out of bounds")
	}
	marshaledEnvelope := block.Data.Data[index]
	envelope, err := GetEnvelopeFromBlock(marshaledEnvelope)
	err = errors.WithMessagef(err, "block data does not carry an envelope at index %d", index)
	return envelope, err
}

// MakeChannelHeader creates a ChannelHeader.
func MakeChannelHeader(headerType cb.HeaderType, version int32, chainID string, epoch uint64) *cb.ChannelHeader {
	return &cb.ChannelHeader{
		Type:    int32(headerType),
		Version: version,
		Timestamp: &timestamp.Timestamp{
			Seconds: time.Now().Unix(),
			Nanos:   0,
		},
		ChannelId: chainID,
		Epoch:     epoch,
	}
}

// MakeSignatureHeader creates a SignatureHeader.
func MakeSignatureHeader(serializedCreatorCertChain []byte, nonce []byte) *cb.SignatureHeader {
	return &cb.SignatureHeader{
		Creator: serializedCreatorCertChain,
		Nonce:   nonce,
	}
}

// SetTxID generates a transaction id based on the provided signature header
// and sets the TxId field in the channel header
func SetTxID(channelHeader *cb.ChannelHeader, signatureHeader *cb.SignatureHeader) {
	channelHeader.TxId = ComputeTxID(
		signatureHeader.Nonce,
		signatureHeader.Creator,
	)
}

// MakePayloadHeader creates a Payload Header.
func MakePayloadHeader(ch *cb.ChannelHeader, sh *cb.SignatureHeader) *cb.Header {
	return &cb.Header{
		ChannelHeader:   MarshalOrPanic(ch),
		SignatureHeader: MarshalOrPanic(sh),
	}
}

// NewSignatureHeader returns a SignatureHeader with a valid nonce.
func NewSignatureHeader(id identity.Serializer) (*cb.SignatureHeader, error) {
	creator, err := id.Serialize()
	if err != nil {
		return nil, err
	}
	nonce, err := CreateNonce()
	if err != nil {
		return nil, err
	}

	return &cb.SignatureHeader{
		Creator: creator,
		Nonce:   nonce,
	}, nil
}

// NewSignatureHeaderOrPanic returns a signature header and panics on error.
func NewSignatureHeaderOrPanic(id identity.Serializer) *cb.SignatureHeader {
	if id == nil {
		panic(errors.New("invalid signer. cannot be nil"))
	}

	signatureHeader, err := NewSignatureHeader(id)
	if err != nil {
		panic(fmt.Errorf("failed generating a new SignatureHeader: %s", err))
	}

	return signatureHeader
}

// SignOrPanic signs a message and panics on error.
func SignOrPanic(signer identity.Signer, msg []byte) []byte {
	if signer == nil {
		panic(errors.New("invalid signer. cannot be nil"))
	}

	sigma, err := signer.Sign(msg)
	if err != nil {
		panic(fmt.Errorf("failed generating signature: %s", err))
	}
	return sigma
}

// IsConfigBlock validates whenever given block contains configuration
// update transaction
func IsConfigBlock(block *cb.Block) bool {
	if block.Data == nil {
		return false
	}

	if len(block.Data.Data) != 1 {
		return false
	}

	marshaledEnvelope := block.Data.Data[0]
	envelope, err := GetEnvelopeFromBlock(marshaledEnvelope)
	if err != nil {
		return false
	}

	payload, err := UnmarshalPayload(envelope.Payload)
	if err != nil {
		return false
	}

	if payload.Header == nil {
		return false
	}

	hdr, err := UnmarshalChannelHeader(payload.Header.ChannelHeader)
	if err != nil {
		return false
	}

	return cb.HeaderType(hdr.Type) == cb.HeaderType_CONFIG || cb.HeaderType(hdr.Type) == cb.HeaderType_ORDERER_TRANSACTION
}

// ChannelHeader returns the *cb.ChannelHeader for a given *cb.Envelope.
func ChannelHeader(env *cb.Envelope) (*cb.ChannelHeader, error) {
	if env == nil {
		return nil, errors.New("Invalid envelope payload. can't be nil")
	}

	envPayload, err := UnmarshalPayload(env.Payload)
	if err != nil {
		return nil, err
	}

	if envPayload.Header == nil {
		return nil, errors.New("header not set")
	}

	if envPayload.Header.ChannelHeader == nil {
		return nil, errors.New("channel header not set")
	}

	chdr, err := UnmarshalChannelHeader(envPayload.Header.ChannelHeader)
	if err != nil {
		return nil, errors.WithMessage(err, "error unmarshaling channel header")
	}

	return chdr, nil
}

// ChannelID returns the Channel ID for a given *cb.Envelope.
func ChannelID(env *cb.Envelope) (string, error) {
	chdr, err := ChannelHeader(env)
	if err != nil {
		return "", errors.WithMessage(err, "error retrieving channel header")
	}

	return chdr.ChannelId, nil
}

// EnvelopeToConfigUpdate is used to extract a ConfigUpdateEnvelope from an envelope of
// type CONFIG_UPDATE
func EnvelopeToConfigUpdate(configtx *cb.Envelope) (*cb.ConfigUpdateEnvelope, error) {
	configUpdateEnv := &cb.ConfigUpdateEnvelope{}
	_, err := UnmarshalEnvelopeOfType(configtx, cb.HeaderType_CONFIG_UPDATE, configUpdateEnv)
	if err != nil {
		return nil, err
	}
	return configUpdateEnv, nil
}

func getRandomNonce() ([]byte, error) {
	key := make([]byte, 24)

	_, err := rand.Read(key)
	if err != nil {
		return nil, errors.Wrap(err, "error getting random bytes")
	}
	return key, nil
}
```
