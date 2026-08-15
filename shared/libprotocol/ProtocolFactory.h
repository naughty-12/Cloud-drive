#ifndef PROTOCOLFACTORY_H
#define PROTOCOLFACTORY_H

/**
 * @file ProtocolFactory.h
 * @brief Serialization / deserialization factory for all protocol packets.
 *
 * ProtocolFactory bridges the gap between the legacy fixed-struct Packdef.h
 * types and the new BinaryStream serialization engine. It provides:
 *
 *   - parseType():    Extract the m_ntype from a raw packet body.
 *   - serialize*():   Convert a struct to a wire-format byte vector
 *                     (BinaryStream → wrap with 4-byte length prefix).
 *   - deserialize*(): Parse a raw packet body into a struct
 *                     (BinaryStream::fromData → extract fields).
 *
 * All serialize methods produce packets in this format:
 *   [4 bytes: total body length (big-endian)] [body: BinaryStream payload]
 *
 * This is wire-compatible with the existing INet::sendData protocol.
 */

#include "Packdef.h"
#include "BinaryStream.h"

#include <cstdint>
#include <vector>

class ProtocolFactory {
public:
    // -----------------------------------------------------------------------
    // Packet Type Detection
    // -----------------------------------------------------------------------

    /**
     * @brief Extract the protocol type (m_ntype) from a raw packet body.
     *
     * The body is the payload AFTER the 4-byte length prefix.
     * The first byte of every body is the m_ntype field.
     *
     * @param packetBody Pointer to the start of the packet body.
     * @param bodyLen    Length of the body in bytes (must be >= 1).
     * @return The m_ntype value (2-29 for valid protocols).
     * @throws std::runtime_error if bodyLen < 1.
     */
    static char parseType(const char* packetBody, int bodyLen);

    // -----------------------------------------------------------------------
    // Packet Wrapping
    // -----------------------------------------------------------------------

    /**
     * @brief Wrap a BinaryStream body with a 4-byte big-endian length prefix.
     *
     * This produces the complete wire-format packet:
     *   [4 bytes: bodyLen (big-endian, 32-bit)] [body bytes]
     *
     * @param body The serialized body payload.
     * @return The complete wire-format packet.
     */
    static std::vector<uint8_t> wrapPacket(const std::vector<uint8_t>& body);

    // =======================================================================
    // Serialize Methods (Struct → std::vector<uint8_t> wire format)
    // =======================================================================

    // -- Register --
    static std::vector<uint8_t> serializeRegisterRQ(const STRU_REGISTERRQ& s);
    static std::vector<uint8_t> serializeRegisterRS(const STRU_REGISTERRS& s);

    // -- Login --
    static std::vector<uint8_t> serializeLoginRQ(const STRU_LOGINRQ& s);
    static std::vector<uint8_t> serializeLoginRS(const STRU_LOGINRS& s);

    // -- Get File List --
    static std::vector<uint8_t> serializeGetFileListRQ(const STRU_GETFILELISTRQ& s);
    static std::vector<uint8_t> serializeGetFileListRS(const STRU_GETFILELISTRS& s);

    // -- Upload File Info --
    static std::vector<uint8_t> serializeUploadFileInfoRQ(const STRU_UPLOADFILEINFORQ& s);
    static std::vector<uint8_t> serializeUploadFileInfoRS(const STRU_UPLOADFILEINFORS& s);

    // -- Upload File Block --
    static std::vector<uint8_t> serializeUploadFileBlockRQ(const STRU_UPLOADFILEBLOCKRQ& s);
    static std::vector<uint8_t> serializeUploadFileBlockRS(const STRU_UPLOADFILEBLOCKRS& s);

    // -- Download File Info --
    static std::vector<uint8_t> serializeDownloadFileInfoRQ(const STRU_DOWNLOADFILEINFORQ& s);
    static std::vector<uint8_t> serializeDownloadFileInfoRS(const STRU_DOWNLOADFILEINFORS& s);

    // -- Download File Block --
    static std::vector<uint8_t> serializeDownloadFileBlockRQ(const STRU_DOWNLOADFILEBLOCKRQ& s);
    static std::vector<uint8_t> serializeDownloadFileBlockRS(const STRU_DOWNLOADFILEBLOCKRS& s);

    // -- Search File --
    static std::vector<uint8_t> serializeSearchFileRQ(const STRU_SEARCHFILERQ& s);
    static std::vector<uint8_t> serializeSearchFileRS(const STRU_SEARCHFILERS& s);

    // -- Delete File --
    static std::vector<uint8_t> serializeDeleteFileRQ(const STRU_DELETEFILERQ& s);
    static std::vector<uint8_t> serializeDeleteFileRS(const STRU_DELETEFILERS& s);

    // -- Share File --
    static std::vector<uint8_t> serializeShareFileRQ(const STRU_SHAREFILERQ& s);
    static std::vector<uint8_t> serializeShareFileRS(const STRU_SHAREFILERS& s);

    // -- Delete Share File (F10-4) --
    static std::vector<uint8_t> serializeDeleteShareRQ(const STRU_DELETESHARERQ& s);
    static std::vector<uint8_t> serializeDeleteShareRS(const STRU_DELETESHARERS& s);

    // -- Sparse Fingerprint Pre-check (L2 upload funnel) --
    static std::vector<uint8_t> serializeSparseCheckRQ(const STRU_SPARSECHECKRQ& s);
    static std::vector<uint8_t> serializeSparseCheckRS(const STRU_SPARSECHECKRS& s);

    // -- Get File (Extract) --
    static std::vector<uint8_t> serializeGetFileRQ(const STRU_GETFILERQ& s);
    static std::vector<uint8_t> serializeGetFileRS(const STRU_GETFILERS& s);

    // -- AI Preview (Phase 3) --
    static std::vector<uint8_t> serializeAIPreviewRQ(const STRU_AIPREVIEWRQ& s);
    static std::vector<uint8_t> serializeAIPreviewRS(const STRU_AIPREVIEWRS& s);

    // -- AI Search (Phase 3) --
    static std::vector<uint8_t> serializeAISearchRQ(const STRU_AISEARCHRQ& s);
    static std::vector<uint8_t> serializeAISearchRS(const STRU_AISEARCHRS& s);

    // -- AI Tag (Phase 3) --
    static std::vector<uint8_t> serializeAITagRQ(const STRU_AITAGRQ& s);
    static std::vector<uint8_t> serializeAITagRS(const STRU_AITAGRS& s);

    // -- Stream Token (Phase 2: HTTP streaming) --
    static std::vector<uint8_t> serializeStreamTokenRQ(const STRU_STREAMTOKENRQ& s);
    static std::vector<uint8_t> serializeStreamTokenRS(const STRU_STREAMTOKENRS& s);

    // -- Cluster: Replicate Block (Phase: Distributed L2) --
    static std::vector<uint8_t> serializeReplicateBlockRQ(const STRU_REPLICATEBLOCKRQ& s);
    static std::vector<uint8_t> serializeReplicateBlockRS(const STRU_REPLICATEBLOCKRS& s);

    // -- Cluster: Redirect (Phase: Distributed L2) --
    static std::vector<uint8_t> serializeRedirectRS(const STRU_REDIRECTRS& s);

    // =======================================================================
    // Deserialize Methods (Raw packet body → Struct)
    // All RQ types — RS deserialization is done on the client side
    // =======================================================================

    /**
     * @brief Deserialize a raw packet body into a struct.
     *
     * The body is the payload AFTER the 4-byte length prefix,
     * and AFTER the m_ntype byte has been consumed (m_ntype is read
     * separately by parseType() before calling deserialize).
     *
     * @param body  Pointer to body bytes AFTER the m_ntype byte.
     * @param len   Length of the remaining body.
     * @return The deserialized struct.
     */
    static STRU_REGISTERRQ          deserializeRegisterRQ(const char* body, int len);
    static STRU_LOGINRQ             deserializeLoginRQ(const char* body, int len);
    static STRU_GETFILELISTRQ       deserializeGetFileListRQ(const char* body, int len);
    static STRU_UPLOADFILEINFORQ    deserializeUploadFileInfoRQ(const char* body, int len);
    static STRU_UPLOADFILEBLOCKRQ   deserializeUploadFileBlockRQ(const char* body, int len);
    static STRU_DOWNLOADFILEINFORQ  deserializeDownloadFileInfoRQ(const char* body, int len);
    static STRU_DOWNLOADFILEBLOCKRQ deserializeDownloadFileBlockRQ(const char* body, int len);
    static STRU_SEARCHFILERQ        deserializeSearchFileRQ(const char* body, int len);
    static STRU_DELETEFILERQ        deserializeDeleteFileRQ(const char* body, int len);
    static STRU_SHAREFILERQ         deserializeShareFileRQ(const char* body, int len);
    static STRU_DELETESHARERQ       deserializeDeleteShareRQ(const char* body, int len);
    static STRU_SPARSECHECKRQ       deserializeSparseCheckRQ(const char* body, int len);
    static STRU_GETFILERQ           deserializeGetFileRQ(const char* body, int len);
    static STRU_AIPREVIEWRQ         deserializeAIPreviewRQ(const char* body, int len);
    static STRU_AISEARCHRQ          deserializeAISearchRQ(const char* body, int len);
    static STRU_AITAGRQ             deserializeAITagRQ(const char* body, int len);

    // Cluster: Replicate Block + Redirect
    static STRU_REPLICATEBLOCKRQ   deserializeReplicateBlockRQ(const char* body, int len);
    static STRU_REPLICATEBLOCKRS   deserializeReplicateBlockRS(const char* body, int len);
    static STRU_REDIRECTRS         deserializeRedirectRS(const char* body, int len);

    // Stream Token (Phase 2: HTTP streaming)
    static STRU_STREAMTOKENRQ      deserializeStreamTokenRQ(const char* body, int len);
    static STRU_STREAMTOKENRS      deserializeStreamTokenRS(const char* body, int len);

    // RS types — deserialization (client-side)
    static STRU_REGISTERRS         deserializeRegisterRS(const char* body, int len);
    static STRU_LOGINRS            deserializeLoginRS(const char* body, int len);
    static STRU_GETFILELISTRS      deserializeGetFileListRS(const char* body, int len);
    static STRU_UPLOADFILEINFORS   deserializeUploadFileInfoRS(const char* body, int len);
    static STRU_UPLOADFILEBLOCKRS  deserializeUploadFileBlockRS(const char* body, int len);
    static STRU_DOWNLOADFILEINFORS deserializeDownloadFileInfoRS(const char* body, int len);
    static STRU_DOWNLOADFILEBLOCKRS deserializeDownloadFileBlockRS(const char* body, int len);
    static STRU_SEARCHFILERS       deserializeSearchFileRS(const char* body, int len);
    static STRU_DELETEFILERS       deserializeDeleteFileRS(const char* body, int len);
    static STRU_SHAREFILERS        deserializeShareFileRS(const char* body, int len);
    static STRU_DELETESHARERS      deserializeDeleteShareRS(const char* body, int len);
    static STRU_SPARSECHECKRS      deserializeSparseCheckRS(const char* body, int len);
    static STRU_GETFILERS          deserializeGetFileRS(const char* body, int len);
    static STRU_AIPREVIEWRS        deserializeAIPreviewRS(const char* body, int len);
    static STRU_AISEARCHRS         deserializeAISearchRS(const char* body, int len);
    static STRU_AITAGRS            deserializeAITagRS(const char* body, int len);

private:
    /// Helper: serialize a FILEINFO into a BinaryStream.
    static void serializeFileInfo(BinaryStream& bs, const FILEINFO& info);

    /// Helper: deserialize a FILEINFO from a BinaryStream.
    static FILEINFO deserializeFileInfo(BinaryStream& bs);
};

#endif // PROTOCOLFACTORY_H
