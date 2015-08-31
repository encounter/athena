#include <stdio.h>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Version.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/CommandLine.h"

static unsigned AthenaError = 0;
#define ATHENA_DNA_BASETYPE "struct Athena::io::DNA"
#define ATHENA_DNA_YAMLTYPE "struct Athena::io::DNAYaml"
#define ATHENA_DNA_READER "__dna_reader"
#define ATHENA_DNA_WRITER "__dna_writer"
#define ATHENA_YAML_READER "__dna_docin"
#define ATHENA_YAML_WRITER "__dna_docout"

#ifndef INSTALL_PREFIX
#define INSTALL_PREFIX /usr/local
#endif
#define XSTR(s) STR(s)
#define STR(s) #s

static llvm::cl::opt<bool> Help("h", llvm::cl::desc("Alias for -help"), llvm::cl::Hidden);

static llvm::cl::OptionCategory ATDNAFormatCategory("atdna options");

static llvm::cl::opt<std::string> OutputFilename("o",
                                                 llvm::cl::desc("Specify output filename"),
                                                 llvm::cl::value_desc("filename"),
                                                 llvm::cl::Prefix);

static llvm::cl::opt<bool> FExceptions("fexceptions",
                                       llvm::cl::desc("Enable C++ Exceptions"));
static llvm::cl::opt<bool> FMSCompat("fms-compatibility",
                                     llvm::cl::desc("Enable MS header compatibility"));
static llvm::cl::opt<std::string> FMSCompatVersion("fms-compatibility-version",
    llvm::cl::desc("Specify MS compatibility version (18.00 for VS2013, 19.00 for VS2015)"));

static llvm::cl::list<std::string> InputFilenames(llvm::cl::Positional,
                                                  llvm::cl::desc("<Input files>"),
                                                  llvm::cl::OneOrMore);

static llvm::cl::list<std::string> IncludeSearchPaths("I",
                                                      llvm::cl::desc("Header search path"),
                                                      llvm::cl::Prefix);

static llvm::cl::list<std::string> SystemIncludeSearchPaths("isystem",
                                                            llvm::cl::desc("System Header search path"));

static llvm::cl::list<std::string> PreprocessorDefines("D",
                                                       llvm::cl::desc("Preprocessor define"),
                                                       llvm::cl::Prefix);

static llvm::cl::opt<bool> EmitIncludes("emit-includes",
                                        llvm::cl::desc("Emit DNA for included files (not just main file)"));

/* LLVM 3.7 changed the stream type */
#if LLVM_VERSION_MAJOR > 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 7)
using StreamOut = llvm::raw_pwrite_stream;
#else
using StreamOut = llvm::raw_fd_ostream;
#endif

class ATDNAEmitVisitor : public clang::RecursiveASTVisitor<ATDNAEmitVisitor>
{
    clang::ASTContext& context;
    StreamOut& fileOut;

    bool isDNARecord(const clang::CXXRecordDecl* record, std::string& baseDNA, bool& isYAML)
    {
        for (const clang::CXXBaseSpecifier& base : record->bases())
        {
            const clang::QualType qtp = base.getType().getCanonicalType();
            if (!qtp.getAsString().compare(0, sizeof(ATHENA_DNA_YAMLTYPE)-1, ATHENA_DNA_YAMLTYPE))
            {
                isYAML = true;
                return true;
            }
        }
        for (const clang::CXXBaseSpecifier& base : record->bases())
        {
            const clang::QualType qtp = base.getType().getCanonicalType();
            if (!qtp.getAsString().compare(0, sizeof(ATHENA_DNA_BASETYPE)-1, ATHENA_DNA_BASETYPE))
                return true;
        }
        for (const clang::CXXBaseSpecifier& base : record->bases())
        {
            clang::QualType qtp = base.getType().getCanonicalType();
            const clang::Type* tp = qtp.getTypePtrOrNull();
            if (tp)
            {
                const clang::CXXRecordDecl* rDecl = tp->getAsCXXRecordDecl();
                if (rDecl)
                {
                    if (isDNARecord(rDecl, baseDNA, isYAML))
                    {
                        bool hasRead = false;
                        bool hasWrite = false;
                        for (const clang::CXXMethodDecl* method : rDecl->methods())
                        {
                            std::string compName = method->getDeclName().getAsString();
                            if (!compName.compare("read"))
                                hasRead = true;
                            else if (!compName.compare("write"))
                                hasWrite = true;
                        }
                        if (hasRead && hasWrite)
                            baseDNA = rDecl->getName();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    std::string GetOpString(const clang::Type* theType, unsigned width,
                            const std::string& fieldName, bool writerPass,
                            const std::string& funcPrefix, bool& isDNATypeOut)
    {
        isDNATypeOut = false;
        if (writerPass)
        {
            if (theType->isEnumeralType())
            {
                clang::EnumType* eType = (clang::EnumType*)theType;
                clang::EnumDecl* eDecl = eType->getDecl();
                theType = eDecl->getIntegerType().getCanonicalType().getTypePtr();

                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_DNA_WRITER ".writeBool(bool(" + fieldName + "));";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_WRITER ".writeUByte(atUint8(" + fieldName + "));";
                    else if (width == 16)
                        return ATHENA_DNA_WRITER ".writeUint16" + funcPrefix + "(atUint16(" + fieldName + "));";
                    else if (width == 32)
                        return ATHENA_DNA_WRITER ".writeUint32" + funcPrefix + "(atUint32(" + fieldName + "));";
                    else if (width == 64)
                        return ATHENA_DNA_WRITER ".writeUint64" + funcPrefix + "(atUint64(" + fieldName + "));";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_WRITER ".writeByte(atInt8(" + fieldName + "));";
                    else if (width == 16)
                        return ATHENA_DNA_WRITER ".writeInt16" + funcPrefix + "(atInt16(" + fieldName + "));";
                    else if (width == 32)
                        return ATHENA_DNA_WRITER ".writeInt32" + funcPrefix + "(atInt32(" + fieldName + "));";
                    else if (width == 64)
                        return ATHENA_DNA_WRITER ".writeInt64" + funcPrefix + "(atInt64(" + fieldName + "));";
                }
            }
            else if (theType->isBuiltinType())
            {
                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_DNA_WRITER ".writeBool(" + fieldName + ");";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_WRITER ".writeUByte(" + fieldName + ");";
                    else if (width == 16)
                        return ATHENA_DNA_WRITER ".writeUint16" + funcPrefix + "(" + fieldName + ");";
                    else if (width == 32)
                        return ATHENA_DNA_WRITER ".writeUint32" + funcPrefix + "(" + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_DNA_WRITER ".writeUint64" + funcPrefix + "(" + fieldName + ");";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_WRITER ".writeByte(" + fieldName + ");";
                    else if (width == 16)
                        return ATHENA_DNA_WRITER ".writeInt16" + funcPrefix + "(" + fieldName + ");";
                    else if (width == 32)
                        return ATHENA_DNA_WRITER ".writeInt32" + funcPrefix + "(" + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_DNA_WRITER ".writeInt64" + funcPrefix + "(" + fieldName + ");";
                }
                else if (bType->isFloatingPoint())
                {
                    if (width == 32)
                        return ATHENA_DNA_WRITER ".writeFloat" + funcPrefix + "(" + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_DNA_WRITER ".writeDouble" + funcPrefix + "(" + fieldName + ");";
                }
            }
            else if (theType->isRecordType())
            {
                const clang::CXXRecordDecl* rDecl = theType->getAsCXXRecordDecl();
                for (const clang::FieldDecl* field : rDecl->fields())
                {
                    if (!field->getName().compare("clangVec"))
                    {
                        const clang::VectorType* vType = (clang::VectorType*)field->getType().getTypePtr();
                        if (vType->isVectorType())
                        {
                            const clang::BuiltinType* eType = (clang::BuiltinType*)vType->getElementType().getTypePtr();
                            if (!eType->isBuiltinType() || !eType->isFloatingPoint() ||
                                context.getTypeInfo(eType).Width != 32)
                                continue;
                            if (vType->getNumElements() == 2)
                                return ATHENA_DNA_WRITER ".writeVec2f" + funcPrefix + "(" + fieldName + ");";
                            else if (vType->getNumElements() == 3)
                                return ATHENA_DNA_WRITER ".writeVec3f" + funcPrefix + "(" + fieldName + ");";
                            else if (vType->getNumElements() == 4)
                                return ATHENA_DNA_WRITER ".writeVec4f" + funcPrefix + "(" + fieldName + ");";
                        }
                    }
                }
                std::string baseDNA;
                bool isYAML = false;
                if (isDNARecord(rDecl, baseDNA, isYAML))
                {
                    isDNATypeOut = true;
                    return "write(" ATHENA_DNA_WRITER ");";
                }
            }
        }
        else
        {
            if (theType->isEnumeralType())
            {
                clang::EnumType* eType = (clang::EnumType*)theType;
                clang::EnumDecl* eDecl = eType->getDecl();
                theType = eDecl->getIntegerType().getCanonicalType().getTypePtr();

                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readBool())";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readUByte())";
                    else if (width == 16)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readUint16" + funcPrefix + "())";
                    else if (width == 32)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readUint32" + funcPrefix + "())";
                    else if (width == 64)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readUint64" + funcPrefix + "())";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readByte()";
                    else if (width == 16)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readInt16" + funcPrefix + "())";
                    else if (width == 32)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readInt32" + funcPrefix + "())";
                    else if (width == 64)
                        return eDecl->getName().str() + "(" ATHENA_DNA_READER ".readInt64" + funcPrefix + "())";
                }
            }
            else if (theType->isBuiltinType())
            {
                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_DNA_READER ".readBool()";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_READER ".readUByte()";
                    else if (width == 16)
                        return ATHENA_DNA_READER ".readUint16" + funcPrefix + "()";
                    else if (width == 32)
                        return ATHENA_DNA_READER ".readUint32" + funcPrefix + "()";
                    else if (width == 64)
                        return ATHENA_DNA_READER ".readUint64" + funcPrefix + "()";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_DNA_READER ".readByte()";
                    else if (width == 16)
                        return ATHENA_DNA_READER ".readInt16" + funcPrefix + "()";
                    else if (width == 32)
                        return ATHENA_DNA_READER ".readInt32" + funcPrefix + "()";
                    else if (width == 64)
                        return ATHENA_DNA_READER ".readInt64" + funcPrefix + "()";
                }
                else if (bType->isFloatingPoint())
                {
                    if (width == 32)
                        return ATHENA_DNA_READER ".readFloat" + funcPrefix + "()";
                    else if (width == 64)
                        return ATHENA_DNA_READER ".readDouble" + funcPrefix + "()";
                }
            }
            else if (theType->isRecordType())
            {
                const clang::CXXRecordDecl* rDecl = theType->getAsCXXRecordDecl();
                for (const clang::FieldDecl* field : rDecl->fields())
                {
                    if (!field->getName().compare("clangVec"))
                    {
                        const clang::VectorType* vType = (clang::VectorType*)field->getType().getTypePtr();
                        if (vType->isVectorType())
                        {
                            const clang::BuiltinType* eType = (clang::BuiltinType*)vType->getElementType().getTypePtr();
                            if (!eType->isBuiltinType() || !eType->isFloatingPoint() ||
                                context.getTypeInfo(eType).Width != 32)
                                continue;
                            if (vType->getNumElements() == 2)
                                return ATHENA_DNA_READER ".readVec2f" + funcPrefix + "()";
                            else if (vType->getNumElements() == 3)
                                return ATHENA_DNA_READER ".readVec3f" + funcPrefix + "()";
                            else if (vType->getNumElements() == 4)
                                return ATHENA_DNA_READER ".readVec4f" + funcPrefix + "()";
                        }
                    }
                }
                std::string baseDNA;
                bool isYAML = false;
                if (isDNARecord(rDecl, baseDNA, isYAML))
                {
                    isDNATypeOut = true;
                    return "read(" ATHENA_DNA_READER ");";
                }
            }
        }
        return std::string();
    }

    std::string GetYAMLString(const clang::Type* theType, unsigned width,
                              const std::string& fieldName, const std::string& bareFieldName,
                              bool writerPass, bool& isDNATypeOut)
    {
        isDNATypeOut = false;
        if (writerPass)
        {
            if (theType->isEnumeralType())
            {
                clang::EnumType* eType = (clang::EnumType*)theType;
                clang::EnumDecl* eDecl = eType->getDecl();
                theType = eDecl->getIntegerType().getCanonicalType().getTypePtr();

                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_YAML_WRITER ".writeBool(\"" + bareFieldName + "\", bool(" + fieldName + "));";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_WRITER ".writeUByte(\"" + bareFieldName + "\", atUint8(" + fieldName + "));";
                    else if (width == 16)
                        return ATHENA_YAML_WRITER ".writeUint16(\"" + bareFieldName + "\", atUint16(" + fieldName + "));";
                    else if (width == 32)
                        return ATHENA_YAML_WRITER ".writeUint32(\"" + bareFieldName + "\", atUint32(" + fieldName + "));";
                    else if (width == 64)
                        return ATHENA_YAML_WRITER ".writeUint64(\"" + bareFieldName + "\", atUint64(" + fieldName + "));";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_WRITER ".writeByte(\"" + bareFieldName + "\", atInt8(" + fieldName + "));";
                    else if (width == 16)
                        return ATHENA_YAML_WRITER ".writeInt16(\"" + bareFieldName + "\", atInt16(" + fieldName + "));";
                    else if (width == 32)
                        return ATHENA_YAML_WRITER ".writeInt32(\"" + bareFieldName + "\", atInt32(" + fieldName + "));";
                    else if (width == 64)
                        return ATHENA_YAML_WRITER ".writeInt64(\"" + bareFieldName + "\", atInt64(" + fieldName + "));";
                }
            }
            else if (theType->isBuiltinType())
            {
                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_YAML_WRITER ".writeBool(\"" + bareFieldName + "\", " + fieldName + ");";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_WRITER ".writeUByte(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 16)
                        return ATHENA_YAML_WRITER ".writeUint16(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 32)
                        return ATHENA_YAML_WRITER ".writeUint32(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_YAML_WRITER ".writeUint64(\"" + bareFieldName + "\", " + fieldName + ");";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_WRITER ".writeByte(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 16)
                        return ATHENA_YAML_WRITER ".writeInt16(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 32)
                        return ATHENA_YAML_WRITER ".writeInt32(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_YAML_WRITER ".writeInt64(\"" + bareFieldName + "\", " + fieldName + ");";
                }
                else if (bType->isFloatingPoint())
                {
                    if (width == 32)
                        return ATHENA_YAML_WRITER ".writeFloat(\"" + bareFieldName + "\", " + fieldName + ");";
                    else if (width == 64)
                        return ATHENA_YAML_WRITER ".writeDouble(\"" + bareFieldName + "\", " + fieldName + ");";
                }
            }
            else if (theType->isRecordType())
            {
                const clang::CXXRecordDecl* rDecl = theType->getAsCXXRecordDecl();
                for (const clang::FieldDecl* field : rDecl->fields())
                {
                    if (!field->getName().compare("clangVec"))
                    {
                        const clang::VectorType* vType = (clang::VectorType*)field->getType().getTypePtr();
                        if (vType->isVectorType())
                        {
                            const clang::BuiltinType* eType = (clang::BuiltinType*)vType->getElementType().getTypePtr();
                            if (!eType->isBuiltinType() || !eType->isFloatingPoint() ||
                                context.getTypeInfo(eType).Width != 32)
                                continue;
                            if (vType->getNumElements() == 2)
                                return ATHENA_YAML_WRITER ".writeVec2f(\"" + bareFieldName + "\", " + fieldName + ");";
                            else if (vType->getNumElements() == 3)
                                return ATHENA_YAML_WRITER ".writeVec3f(\"" + bareFieldName + "\", " + fieldName + ");";
                            else if (vType->getNumElements() == 4)
                                return ATHENA_YAML_WRITER ".writeVec4f(\"" + bareFieldName + "\", " + fieldName + ");";
                        }
                    }
                }
                std::string baseDNA;
                bool isYAML = false;
                if (isDNARecord(rDecl, baseDNA, isYAML))
                {
                    isDNATypeOut = true;
                    return "toYAML(" ATHENA_YAML_WRITER ");";
                }
            }
        }
        else
        {
            if (theType->isEnumeralType())
            {
                clang::EnumType* eType = (clang::EnumType*)theType;
                clang::EnumDecl* eDecl = eType->getDecl();
                theType = eDecl->getIntegerType().getCanonicalType().getTypePtr();

                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readBool(\"" + bareFieldName + "\"))";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readUByte(\"" + bareFieldName + "\"))";
                    else if (width == 16)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readUint16(\"" + bareFieldName + "\"))";
                    else if (width == 32)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readUint32(\"" + bareFieldName + "\"))";
                    else if (width == 64)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readUint64(\"" + bareFieldName + "\"))";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readByte(\"" + bareFieldName + "\"))";
                    else if (width == 16)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readInt16(\"" + bareFieldName + "\"))";
                    else if (width == 32)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readInt32(\"" + bareFieldName + "\"))";
                    else if (width == 64)
                        return eDecl->getName().str() + "(" ATHENA_YAML_READER ".readInt64(\"" + bareFieldName + "\"))";
                }
            }
            else if (theType->isBuiltinType())
            {
                const clang::BuiltinType* bType = (clang::BuiltinType*)theType;
                if (bType->isBooleanType())
                {
                    return ATHENA_YAML_READER ".readBool(\"" + bareFieldName + "\")";
                }
                else if (bType->isUnsignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_READER ".readUByte(\"" + bareFieldName + "\")";
                    else if (width == 16)
                        return ATHENA_YAML_READER ".readUint16(\"" + bareFieldName + "\")";
                    else if (width == 32)
                        return ATHENA_YAML_READER ".readUint32(\"" + bareFieldName + "\")";
                    else if (width == 64)
                        return ATHENA_YAML_READER ".readUint64(\"" + bareFieldName + "\")";
                }
                else if (bType->isSignedInteger())
                {
                    if (width == 8)
                        return ATHENA_YAML_READER ".readByte(\"" + bareFieldName + "\")";
                    else if (width == 16)
                        return ATHENA_YAML_READER ".readInt16(\"" + bareFieldName + "\")";
                    else if (width == 32)
                        return ATHENA_YAML_READER ".readInt32(\"" + bareFieldName + "\")";
                    else if (width == 64)
                        return ATHENA_YAML_READER ".readInt64(\"" + bareFieldName + "\")";
                }
                else if (bType->isFloatingPoint())
                {
                    if (width == 32)
                        return ATHENA_YAML_READER ".readFloat(\"" + bareFieldName + "\")";
                    else if (width == 64)
                        return ATHENA_YAML_READER ".readDouble(\"" + bareFieldName + "\")";
                }
            }
            else if (theType->isRecordType())
            {
                const clang::CXXRecordDecl* rDecl = theType->getAsCXXRecordDecl();
                for (const clang::FieldDecl* field : rDecl->fields())
                {
                    if (!field->getName().compare("clangVec"))
                    {
                        const clang::VectorType* vType = (clang::VectorType*)field->getType().getTypePtr();
                        if (vType->isVectorType())
                        {
                            const clang::BuiltinType* eType = (clang::BuiltinType*)vType->getElementType().getTypePtr();
                            if (!eType->isBuiltinType() || !eType->isFloatingPoint() ||
                                context.getTypeInfo(eType).Width != 32)
                                continue;
                            if (vType->getNumElements() == 2)
                                return ATHENA_YAML_READER ".readVec2f(\"" + bareFieldName + "\")";
                            else if (vType->getNumElements() == 3)
                                return ATHENA_YAML_READER ".readVec3f(\"" + bareFieldName + "\")";
                            else if (vType->getNumElements() == 4)
                                return ATHENA_YAML_READER ".readVec4f(\"" + bareFieldName + "\")";
                        }
                    }
                }
                std::string baseDNA;
                bool isYAML = false;
                if (isDNARecord(rDecl, baseDNA, isYAML))
                {
                    isDNATypeOut = true;
                    return "fromYAML(" ATHENA_YAML_READER ");";
                }
            }
        }
        return std::string();
    }

    void emitIOFuncs(clang::CXXRecordDecl* decl, const std::string& baseDNA)
    {
        /* Two passes - read then write */
        for (int p=0 ; p<2 ; ++p)
        {
            if (p)
                fileOut << "void " << decl->getQualifiedNameAsString() << "::write(Athena::io::IStreamWriter& " ATHENA_DNA_WRITER ") const\n{\n";
            else
                fileOut << "void " << decl->getQualifiedNameAsString() << "::read(Athena::io::IStreamReader& " ATHENA_DNA_READER ")\n{\n";

            if (baseDNA.size())
            {
                if (p)
                    fileOut << "    " << baseDNA << "::write(" ATHENA_DNA_WRITER ");\n";
                else
                    fileOut << "    " << baseDNA << "::read(" ATHENA_DNA_READER ");\n";
            }

            for (const clang::FieldDecl* field : decl->fields())
            {
                clang::QualType qualType = field->getType();
                clang::TypeInfo regTypeInfo = context.getTypeInfo(qualType);
                const clang::Type* regType = qualType.getTypePtrOrNull();
                while (regType->getTypeClass() == clang::Type::Elaborated ||
                       regType->getTypeClass() == clang::Type::Typedef)
                    regType = regType->getUnqualifiedDesugaredType();

                /* Resolve constant array */
                size_t arraySize = 1;
                bool isArray = false;
                if (regType->getTypeClass() == clang::Type::ConstantArray)
                {
                    isArray = true;
                    const clang::ConstantArrayType* caType = (clang::ConstantArrayType*)regType;
                    arraySize = caType->getSize().getZExtValue();
                    qualType = caType->getElementType();
                    regTypeInfo = context.getTypeInfo(qualType);
                    regType = qualType.getTypePtrOrNull();
                    if (regType->getTypeClass() == clang::Type::Elaborated)
                        regType = regType->getUnqualifiedDesugaredType();
                }

                for (int e=0 ; e<arraySize ; ++e)
                {
                    std::string fieldName;
                    if (isArray)
                    {
                        char subscript[16];
                        snprintf(subscript, 16, "[%d]", e);
                        fieldName = field->getName().str() + subscript;
                    }
                    else
                        fieldName = field->getName();

                    if (regType->getTypeClass() == clang::Type::TemplateSpecialization)
                    {
                        const clang::TemplateSpecializationType* tsType = (const clang::TemplateSpecializationType*)regType;
                        const clang::TemplateDecl* tsDecl = tsType->getTemplateName().getAsTemplateDecl();
                        const clang::TemplateParameterList* classParms = tsDecl->getTemplateParameters();

                        if (!tsDecl->getName().compare("Value"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 2)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(1);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(defArg->getLocStart(), AthenaError);
                                            diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                            diag.AddSourceRange(clang::CharSourceRange(defArg->getSourceRange(), true));
                                        }
                                        continue;
                                    }
                                }
                            }

                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr();
                                    endianExpr = expr;
                                    if (!expr->isIntegerConstantExpr(endian, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(expr->getLocStart(), AthenaError);
                                            diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                            diag.AddSourceRange(clang::CharSourceRange(expr->getSourceRange(), true));
                                        }
                                        continue;
                                    }
                                }
                            }

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                            {
                                if (!p)
                                {
                                    if (endianExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(endianExpr->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(endianExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            std::string funcPrefix;
                            if (endianVal == 0)
                                funcPrefix = "Little";
                            else if (endianVal == 1)
                                funcPrefix = "Big";

                            clang::QualType templateType;
                            std::string ioOp;
                            bool isDNAType = false;
                            const clang::TemplateArgument* typeArg = nullptr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Type)
                                {
                                    typeArg = &arg;
                                    templateType = arg.getAsType().getCanonicalType();
                                    const clang::Type* type = arg.getAsType().getCanonicalType().getTypePtr();
                                    ioOp = GetOpString(type, regTypeInfo.Width, fieldName, p, funcPrefix, isDNAType);
                                }
                            }

                            if (ioOp.empty())
                            {
                                if (!p)
                                {
                                    clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                    diag.AddString("Unable to use type '" + tsDecl->getName().str() + "' with Athena");
                                    diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                }
                                continue;
                            }

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " << ioOp << ";\n";
                            else
                                fileOut << "    " << ioOp << "\n";
                        }
                        else if (!tsDecl->getName().compare("Vector"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 3)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(2);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(defArg->getLocStart(), AthenaError);
                                            diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                            diag.AddSourceRange(clang::CharSourceRange(defArg->getSourceRange(), true));
                                        }
                                        continue;
                                    }
                                }
                            }

                            std::string sizeExpr;
                            const clang::TemplateArgument* sizeArg = nullptr;
                            size_t idx = 0;
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    if (idx == 1)
                                    {
                                        sizeArg = &arg;
                                        const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                        if (uExpr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                            uExpr->getKind() == clang::UETT_SizeOf)
                                        {
                                            const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                            while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                                argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                            llvm::raw_string_ostream strStream(sizeExpr);
                                            argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                        }
                                    }
                                    else if (idx == 2)
                                    {
                                        endianExpr = expr;
                                        if (!expr->isIntegerConstantExpr(endian, context))
                                        {
                                            if (!p)
                                            {
                                                clang::DiagnosticBuilder diag = context.getDiagnostics().Report(expr->getLocStart(), AthenaError);
                                                diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                                diag.AddSourceRange(clang::CharSourceRange(expr->getSourceRange(), true));
                                            }
                                            bad = true;
                                            break;
                                        }
                                    }
                                }
                                ++idx;
                            }
                            if (bad)
                                continue;

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                            {
                                if (!p)
                                {
                                    if (endianExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(endianExpr->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(endianExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            std::string funcPrefix;
                            if (endianVal == 0)
                                funcPrefix = "Little";
                            else if (endianVal == 1)
                                funcPrefix = "Big";

                            clang::QualType templateType;
                            std::string ioOp;
                            bool isDNAType = false;
                            const clang::TemplateArgument* typeArg = nullptr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Type)
                                {
                                    typeArg = &arg;
                                    templateType = arg.getAsType().getCanonicalType();
                                    clang::TypeInfo typeInfo = context.getTypeInfo(templateType);
                                    static const std::string elemStr = "elem";
                                    ioOp = GetOpString(templateType.getTypePtr(), typeInfo.Width, elemStr, p, funcPrefix, isDNAType);
                                }
                            }

                            if (ioOp.empty())
                            {
                                if (!p)
                                {
                                    clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                    diag.AddString("Unable to use type '" + templateType.getAsString() + "' with Athena");
                                    diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                }
                                continue;
                            }

                            if (sizeExpr.empty())
                            {
                                if (!p)
                                {
                                    clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                    diag.AddString("Unable to use count variable with Athena");
                                    diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                }
                                continue;
                            }

                            if (isDNAType)
                                funcPrefix.clear();

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " ATHENA_DNA_READER ".enumerate" << funcPrefix << "(" << fieldName << ", " << sizeExpr << ");\n";
                            else
                                fileOut << "    " ATHENA_DNA_WRITER ".enumerate" << funcPrefix << "(" << fieldName << ");\n";

                        }
                        else if (!tsDecl->getName().compare("Buffer"))
                        {
                            const clang::Expr* sizeExpr = nullptr;
                            std::string sizeExprStr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)arg.getAsExpr()->IgnoreImpCasts();
                                    if (uExpr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                        uExpr->getKind() == clang::UETT_SizeOf)
                                    {
                                        const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                        while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                            argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                        sizeExpr = argExpr;
                                        llvm::raw_string_ostream strStream(sizeExprStr);
                                        argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                    }
                                }
                            }
                            if (sizeExprStr.empty())
                            {
                                if (!p)
                                {
                                    if (sizeExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(sizeExpr->getLocStart(), AthenaError);
                                        diag.AddString("Unable to use size variable with Athena");
                                        diag.AddSourceRange(clang::CharSourceRange(sizeExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Unable to use size variable with Athena");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                            {
                                fileOut << "    " << fieldName << ".reset(new atUint8[" << sizeExprStr << "]);\n"
                                           "    " ATHENA_DNA_READER ".readUBytesToBuf(" << fieldName << ".get(), " << sizeExprStr << ");\n";
                            }
                            else
                            {
                                fileOut << "    " ATHENA_DNA_WRITER ".writeUBytes(" << fieldName << ".get(), " << sizeExprStr << ");\n";
                            }
                        }
                        else if (!tsDecl->getName().compare("String"))
                        {
                            const clang::Expr* sizeExpr = nullptr;
                            std::string sizeExprStr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                    llvm::APSInt sizeLiteral;
                                    if (expr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                        uExpr->getKind() == clang::UETT_SizeOf)
                                    {
                                        const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                        while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                            argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                        sizeExpr = argExpr;
                                        llvm::raw_string_ostream strStream(sizeExprStr);
                                        argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                    }
                                    else if (expr->isIntegerConstantExpr(sizeLiteral, context))
                                    {
                                        sizeExprStr = sizeLiteral.toString(10);
                                    }
                                }
                            }

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_DNA_READER ".readString(" << sizeExprStr << ");\n";
                            else
                            {
                                fileOut << "    " ATHENA_DNA_WRITER ".writeString(" << fieldName;
                                if (sizeExprStr.size())
                                    fileOut << ", " << sizeExprStr;
                                fileOut << ");\n";
                            }
                        }
                        else if (!tsDecl->getName().compare("WString"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 2)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(1);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(defArg->getLocStart(), AthenaError);
                                            diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                            diag.AddSourceRange(clang::CharSourceRange(defArg->getSourceRange(), true));
                                        }
                                        continue;
                                    }
                                }
                            }

                            const clang::Expr* sizeExpr = nullptr;
                            std::string sizeExprStr;
                            size_t idx = 0;
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    if (idx == 0)
                                    {
                                        llvm::APSInt sizeLiteral;
                                        const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                        if (expr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                            uExpr->getKind() == clang::UETT_SizeOf)
                                        {
                                            const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                            while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                                argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                            sizeExpr = argExpr;
                                            llvm::raw_string_ostream strStream(sizeExprStr);
                                            argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                        }
                                        else if (expr->isIntegerConstantExpr(sizeLiteral, context))
                                        {
                                            sizeExprStr = sizeLiteral.toString(10);
                                        }
                                    }
                                    else if (idx == 1)
                                    {
                                        endianExpr = expr;
                                        if (!expr->isIntegerConstantExpr(endian, context))
                                        {
                                            if (!p)
                                            {
                                                clang::DiagnosticBuilder diag = context.getDiagnostics().Report(expr->getLocStart(), AthenaError);
                                                diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                                diag.AddSourceRange(clang::CharSourceRange(expr->getSourceRange(), true));
                                            }
                                            bad = true;
                                            break;
                                        }
                                    }
                                }
                                ++idx;
                            }
                            if (bad)
                                continue;

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                            {
                                if (!p)
                                {
                                    if (endianExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(endianExpr->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(endianExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            std::string funcPrefix;
                            if (endianVal == 0)
                                funcPrefix = "Little";
                            else if (endianVal == 1)
                                funcPrefix = "Big";

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_DNA_READER ".readWString" << funcPrefix << "(" << sizeExprStr << ");\n";
                            else
                            {
                                fileOut << "    " ATHENA_DNA_WRITER ".writeWString" << funcPrefix << "(" << fieldName;
                                if (sizeExprStr.size())
                                    fileOut << ", " << sizeExprStr;
                                fileOut << ");\n";
                            }
                        }
                        else if (!tsDecl->getName().compare("WStringAsString"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 2)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(1);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(defArg->getLocStart(), AthenaError);
                                            diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                            diag.AddSourceRange(clang::CharSourceRange(defArg->getSourceRange(), true));
                                        }
                                        continue;
                                    }
                                }
                            }

                            const clang::Expr* sizeExpr = nullptr;
                            std::string sizeExprStr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                    llvm::APSInt sizeLiteral;
                                    if (expr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                        uExpr->getKind() == clang::UETT_SizeOf)
                                    {
                                        const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                        while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                            argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                        sizeExpr = argExpr;
                                        llvm::raw_string_ostream strStream(sizeExprStr);
                                        argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                    }
                                    else if (expr->isIntegerConstantExpr(sizeLiteral, context))
                                    {
                                        sizeExprStr = sizeLiteral.toString(10);
                                    }
                                }
                            }


                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                            {
                                if (!p)
                                {
                                    if (endianExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(endianExpr->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(endianExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Endian value must be 'BigEndian' or 'LittleEndian'");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            std::string funcPrefix;
                            if (endianVal == 0)
                                funcPrefix = "Little";
                            else if (endianVal == 1)
                                funcPrefix = "Big";

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_DNA_READER ".readWStringAsString" << funcPrefix << "(" << sizeExprStr << ");\n";
                            else
                            {
                                fileOut << "    " ATHENA_DNA_WRITER ".writeStringAsWString" << funcPrefix << "(" << fieldName;
                                if (sizeExprStr.size())
                                    fileOut << ", " << sizeExprStr;
                                fileOut << ");\n";
                            }
                        }
                        else if (!tsDecl->getName().compare("Seek"))
                        {
                            size_t idx = 0;
                            const clang::Expr* offsetExpr = nullptr;
                            std::string offsetExprStr;
                            llvm::APSInt direction(64, 0);
                            const clang::Expr* directionExpr = nullptr;
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    if (!idx)
                                    {
                                        offsetExpr = expr;
                                        const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                        llvm::APSInt offsetLiteral;
                                        if (expr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                            uExpr->getKind() == clang::UETT_SizeOf)
                                        {
                                            const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                            while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                                argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                            offsetExpr = argExpr;
                                            llvm::raw_string_ostream strStream(offsetExprStr);
                                            argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                        }
                                        else if (expr->isIntegerConstantExpr(offsetLiteral, context))
                                        {
                                            offsetExprStr = offsetLiteral.toString(10);
                                        }
                                    }
                                    else
                                    {
                                        directionExpr = expr;
                                        if (!expr->isIntegerConstantExpr(direction, context))
                                        {
                                            if (!p)
                                            {
                                                clang::DiagnosticBuilder diag = context.getDiagnostics().Report(expr->getLocStart(), AthenaError);
                                                diag.AddString("Unable to use non-constant direction expression in Athena");
                                                diag.AddSourceRange(clang::CharSourceRange(expr->getSourceRange(), true));
                                            }
                                            bad = true;
                                            break;
                                        }
                                    }
                                }
                                ++idx;
                            }
                            if (bad)
                                continue;

                            int64_t directionVal = direction.getSExtValue();
                            if (directionVal < 0 || directionVal > 2)
                            {
                                if (!p)
                                {
                                    if (directionExpr)
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(directionExpr->getLocStart(), AthenaError);
                                        diag.AddString("Direction parameter must be 'Begin', 'Current', or 'End'");
                                        diag.AddSourceRange(clang::CharSourceRange(directionExpr->getSourceRange(), true));
                                    }
                                    else
                                    {
                                        clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                                        diag.AddString("Direction parameter must be 'Begin', 'Current', or 'End'");
                                        diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                                    }
                                }
                                continue;
                            }

                            fileOut << "    /* " << fieldName << " */\n";
                            if (directionVal == 0)
                            {
                                if (!p)
                                    fileOut << "    " ATHENA_DNA_READER ".seek(" << offsetExprStr << ", Athena::Begin);\n";
                                else
                                    fileOut << "    " ATHENA_DNA_WRITER ".seek(" << offsetExprStr << ", Athena::Begin);\n";
                            }
                            else if (directionVal == 1)
                            {
                                if (!p)
                                    fileOut << "    " ATHENA_DNA_READER ".seek(" << offsetExprStr << ", Athena::Current);\n";
                                else
                                    fileOut << "    " ATHENA_DNA_WRITER ".seek(" << offsetExprStr << ", Athena::Current);\n";
                            }
                            else if (directionVal == 2)
                            {
                                if (!p)
                                    fileOut << "    " ATHENA_DNA_READER ".seek(" << offsetExprStr << ", Athena::End);\n";
                                else
                                    fileOut << "    " ATHENA_DNA_WRITER ".seek(" << offsetExprStr << ", Athena::End);\n";
                            }

                        }
                        else if (!tsDecl->getName().compare("Align"))
                        {
                            llvm::APSInt align(64, 0);
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr();
                                    if (!expr->isIntegerConstantExpr(align, context))
                                    {
                                        if (!p)
                                        {
                                            clang::DiagnosticBuilder diag = context.getDiagnostics().Report(expr->getLocStart(), AthenaError);
                                            diag.AddString("Unable to use non-constant align expression in Athena");
                                            diag.AddSourceRange(clang::CharSourceRange(expr->getSourceRange(), true));
                                        }
                                        bad = true;
                                        break;
                                    }
                                }
                            }
                            if (bad)
                                continue;

                            int64_t alignVal = align.getSExtValue();
                            if (alignVal)
                            {
                                fileOut << "    /* " << fieldName << " */\n";
                                if (alignVal == 32)
                                {
                                    if (!p)
                                        fileOut << "    " ATHENA_DNA_READER ".seekAlign32();\n";
                                    else
                                        fileOut << "    " ATHENA_DNA_WRITER ".seekAlign32();\n";
                                }
                                else if (align.isPowerOf2())
                                {
                                    if (!p)
                                        fileOut << "    " ATHENA_DNA_READER ".seek((" ATHENA_DNA_READER ".position() + " << alignVal-1 << ") & ~" << alignVal-1 << ", Athena::Begin);\n";
                                    else
                                        fileOut << "    " ATHENA_DNA_WRITER ".seek((" ATHENA_DNA_WRITER ".position() + " << alignVal-1 << ") & ~" << alignVal-1 << ", Athena::Begin);\n";
                                }
                                else
                                {
                                    if (!p)
                                        fileOut << "    " ATHENA_DNA_READER ".seek((" ATHENA_DNA_READER ".position() + " << alignVal-1 << ") / " << alignVal << " * " << alignVal << ", Athena::Begin);\n";
                                    else
                                        fileOut << "    " ATHENA_DNA_WRITER ".seek((" ATHENA_DNA_WRITER ".position() + " << alignVal-1 << ") / " << alignVal << " * " << alignVal << ", Athena::Begin);\n";
                                }
                            }
                        }

                    }

                    else if (regType->getTypeClass() == clang::Type::Record)
                    {
                        const clang::CXXRecordDecl* cxxRDecl = regType->getAsCXXRecordDecl();
                        std::string baseDNA;
                        bool isYAML = false;
                        if (cxxRDecl && isDNARecord(cxxRDecl, baseDNA, isYAML))
                        {
                            fileOut << "    /* " << fieldName << " */\n"
                                       "    " << fieldName << (p ? ".write(" ATHENA_DNA_WRITER ");\n" : ".read(" ATHENA_DNA_READER ");\n");
                            break;
                        }
                    }

                }

            }

            fileOut << "}\n\n";
        }
    }

    void emitYAMLFuncs(clang::CXXRecordDecl* decl, const std::string& baseDNA)
    {
        /* Two passes - read then write */
        for (int p=0 ; p<2 ; ++p)
        {
            if (p)
                fileOut << "void " << decl->getQualifiedNameAsString() << "::toYAML(Athena::io::YAMLDocWriter& " ATHENA_YAML_WRITER ") const\n{\n";
            else
                fileOut << "void " << decl->getQualifiedNameAsString() << "::fromYAML(Athena::io::YAMLDocReader& " ATHENA_YAML_READER ")\n{\n";

            if (baseDNA.size())
            {
                if (p)
                    fileOut << "    " << baseDNA << "::toYAML(" ATHENA_YAML_WRITER ");\n";
                else
                    fileOut << "    " << baseDNA << "::fromYAML(" ATHENA_YAML_READER ");\n";
            }

            for (const clang::FieldDecl* field : decl->fields())
            {
                clang::QualType qualType = field->getType();
                clang::TypeInfo regTypeInfo = context.getTypeInfo(qualType);
                const clang::Type* regType = qualType.getTypePtrOrNull();
                while (regType->getTypeClass() == clang::Type::Elaborated ||
                       regType->getTypeClass() == clang::Type::Typedef)
                    regType = regType->getUnqualifiedDesugaredType();

                /* Resolve constant array */
                size_t arraySize = 1;
                bool isArray = false;
                if (regType->getTypeClass() == clang::Type::ConstantArray)
                {
                    isArray = true;
                    const clang::ConstantArrayType* caType = (clang::ConstantArrayType*)regType;
                    arraySize = caType->getSize().getZExtValue();
                    qualType = caType->getElementType();
                    regTypeInfo = context.getTypeInfo(qualType);
                    regType = qualType.getTypePtrOrNull();
                    if (regType->getTypeClass() == clang::Type::Elaborated)
                        regType = regType->getUnqualifiedDesugaredType();

                    if (!p)
                        fileOut << "    " ATHENA_YAML_READER ".enterSubVector(\"" << field->getName() << "\");\n";
                    else
                        fileOut << "    " ATHENA_YAML_WRITER ".enterSubVector(\"" << field->getName() << "\");\n";
                }

                for (int e=0 ; e<arraySize ; ++e)
                {
                    std::string fieldNameBare = field->getName();
                    std::string fieldName;
                    if (isArray)
                    {
                        char subscript[16];
                        snprintf(subscript, 16, "[%d]", e);
                        fieldName = fieldNameBare + subscript;
                    }
                    else
                        fieldName = fieldNameBare;

                    if (regType->getTypeClass() == clang::Type::TemplateSpecialization)
                    {
                        const clang::TemplateSpecializationType* tsType = (const clang::TemplateSpecializationType*)regType;
                        const clang::TemplateDecl* tsDecl = tsType->getTemplateName().getAsTemplateDecl();
                        const clang::TemplateParameterList* classParms = tsDecl->getTemplateParameters();

                        if (!tsDecl->getName().compare("Value"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 2)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(1);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                        continue;
                                }
                            }

                            clang::QualType templateType;
                            std::string ioOp;
                            bool isDNAType = false;
                            const clang::TemplateArgument* typeArg = nullptr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Type)
                                {
                                    typeArg = &arg;
                                    templateType = arg.getAsType().getCanonicalType();
                                    const clang::Type* type = arg.getAsType().getCanonicalType().getTypePtr();
                                    ioOp = GetYAMLString(type, regTypeInfo.Width, fieldName, fieldNameBare, p, isDNAType);
                                }
                                else if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr();
                                    endianExpr = expr;
                                    if (expr->isIntegerConstantExpr(endian, context))
                                        continue;
                                }
                            }

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                                continue;

                            if (ioOp.empty())
                                continue;

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " << ioOp << ";\n";
                            else
                                fileOut << "    " << ioOp << "\n";
                        }
                        else if (!tsDecl->getName().compare("Vector"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 3)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(2);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                        continue;
                                }
                            }

                            clang::QualType templateType;
                            std::string ioOp;
                            bool isDNAType = false;
                            std::string sizeExpr;
                            const clang::TemplateArgument* typeArg = nullptr;
                            const clang::TemplateArgument* sizeArg = nullptr;
                            size_t idx = 0;
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Type)
                                {
                                    typeArg = &arg;
                                    templateType = arg.getAsType().getCanonicalType();
                                    clang::TypeInfo typeInfo = context.getTypeInfo(templateType);
                                    ioOp = GetYAMLString(templateType.getTypePtr(), typeInfo.Width, "elem", fieldNameBare, p, isDNAType);
                                }
                                else if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    if (idx == 1)
                                    {
                                        sizeArg = &arg;
                                        const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)expr;
                                        if (uExpr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                            uExpr->getKind() == clang::UETT_SizeOf)
                                        {
                                            const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                            while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                                argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                            llvm::raw_string_ostream strStream(sizeExpr);
                                            argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                        }
                                    }
                                    else if (idx == 2)
                                    {
                                        endianExpr = expr;
                                        if (!expr->isIntegerConstantExpr(endian, context))
                                        {
                                            bad = true;
                                            break;
                                        }
                                    }
                                }
                                ++idx;
                            }
                            if (bad)
                                continue;

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                                continue;

                            if (ioOp.empty())
                                continue;

                            if (sizeExpr.empty())
                                continue;

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " ATHENA_YAML_READER ".enumerate(\"" << fieldNameBare << "\", " << fieldName << ", " << sizeExpr << ");\n";
                            else
                                fileOut << "    " ATHENA_YAML_WRITER ".enumerate(\"" << fieldNameBare << "\", " << fieldName << ");\n";

                        }
                        else if (!tsDecl->getName().compare("Buffer"))
                        {
                            const clang::Expr* sizeExpr = nullptr;
                            std::string sizeExprStr;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::UnaryExprOrTypeTraitExpr* uExpr = (clang::UnaryExprOrTypeTraitExpr*)arg.getAsExpr()->IgnoreImpCasts();
                                    if (uExpr->getStmtClass() == clang::Stmt::UnaryExprOrTypeTraitExprClass &&
                                        uExpr->getKind() == clang::UETT_SizeOf)
                                    {
                                        const clang::Expr* argExpr = uExpr->getArgumentExpr();
                                        while (argExpr->getStmtClass() == clang::Stmt::ParenExprClass)
                                            argExpr = ((clang::ParenExpr*)argExpr)->getSubExpr();
                                        sizeExpr = argExpr;
                                        llvm::raw_string_ostream strStream(sizeExprStr);
                                        argExpr->printPretty(strStream, nullptr, context.getPrintingPolicy());
                                    }
                                }
                            }
                            if (sizeExprStr.empty())
                                continue;

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_YAML_READER ".readUBytes(\"" << fieldNameBare << "\");\n";
                            else
                                fileOut << "    " ATHENA_YAML_WRITER ".writeUBytes(\"" << fieldNameBare << "\", " << fieldName << ", " << sizeExprStr << ");\n";
                        }
                        else if (!tsDecl->getName().compare("String") ||
                                 !tsDecl->getName().compare("WStringAsString"))
                        {
                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_YAML_READER ".readString(\"" << fieldNameBare << "\");\n";
                            else
                                fileOut << "    " ATHENA_YAML_WRITER ".writeString(\"" << fieldNameBare << "\", " << fieldName << ");\n";
                        }
                        else if (!tsDecl->getName().compare("WString"))
                        {
                            llvm::APSInt endian(64, -1);
                            const clang::Expr* endianExpr = nullptr;
                            if (classParms->size() >= 2)
                            {
                                const clang::NamedDecl* endianParm = classParms->getParam(1);
                                if (endianParm->getKind() == clang::Decl::NonTypeTemplateParm)
                                {
                                    const clang::NonTypeTemplateParmDecl* nttParm = (clang::NonTypeTemplateParmDecl*)endianParm;
                                    const clang::Expr* defArg = nttParm->getDefaultArgument();
                                    endianExpr = defArg;
                                    if (!defArg->isIntegerConstantExpr(endian, context))
                                        continue;
                                }
                            }

                            size_t idx = 0;
                            bool bad = false;
                            for (const clang::TemplateArgument& arg : *tsType)
                            {
                                if (arg.getKind() == clang::TemplateArgument::Expression)
                                {
                                    const clang::Expr* expr = arg.getAsExpr()->IgnoreImpCasts();
                                    if (idx == 1)
                                    {
                                        endianExpr = expr;
                                        if (!expr->isIntegerConstantExpr(endian, context))
                                        {
                                            bad = true;
                                            break;
                                        }
                                    }
                                }
                                ++idx;
                            }
                            if (bad)
                                continue;

                            int endianVal = endian.getSExtValue();
                            if (endianVal != 0 && endianVal != 1)
                                continue;

                            fileOut << "    /* " << fieldName << " */\n";
                            if (!p)
                                fileOut << "    " << fieldName << " = " ATHENA_YAML_READER ".readWString(\"" << fieldNameBare << "\");\n";
                            else
                                fileOut << "    " ATHENA_YAML_WRITER ".writeWString(\"" << fieldNameBare << "\", " << fieldName << ");\n";
                        }

                    }

                    else if (regType->getTypeClass() == clang::Type::Record)
                    {
                        const clang::CXXRecordDecl* cxxRDecl = regType->getAsCXXRecordDecl();
                        std::string baseDNA;
                        bool isYAML = false;
                        if (cxxRDecl && isDNARecord(cxxRDecl, baseDNA, isYAML))
                        {
                            if (!p)
                            {
                                fileOut << "    /* " << fieldName << " */\n"
                                           "    " ATHENA_YAML_READER ".enumerate(\"" << fieldNameBare << "\", " << fieldName << ");\n";
                            }
                            else
                            {
                                fileOut << "    /* " << fieldName << " */\n"
                                           "    " ATHENA_YAML_WRITER ".enumerate(\"" << fieldNameBare << "\", " << fieldName << ");\n";
                            }
                            break;
                        }
                    }

                }

                if (isArray)
                {
                    if (!p)
                        fileOut << "    " ATHENA_YAML_READER ".leaveSubVector();\n";
                    else
                        fileOut << "    " ATHENA_YAML_WRITER ".leaveSubVector();\n";
                }

            }

            fileOut << "}\n\n";
        }
    }

public:
    explicit ATDNAEmitVisitor(clang::ASTContext& ctxin, StreamOut& fo)
    : context(ctxin), fileOut(fo) {}

    bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl)
    {
        if (!EmitIncludes && !context.getSourceManager().isInMainFile(decl->getLocation()))
            return true;

        if (decl->isInvalidDecl() || !decl->hasDefinition())
            return true;

        if (!decl->getNumBases())
            return true;

        /* First ensure this inherits from struct Athena::io::DNA */
        std::string baseDNA;
        bool isYAML = false;
        if (!isDNARecord(decl, baseDNA, isYAML))
            return true;

        /* Make sure there aren't namespace conflicts or Delete meta type */
        for (const clang::FieldDecl* field : decl->fields())
        {
            if (!field->getName().compare(ATHENA_DNA_READER) ||
                !field->getName().compare(ATHENA_DNA_WRITER))
            {
                clang::DiagnosticBuilder diag = context.getDiagnostics().Report(field->getLocStart(), AthenaError);
                diag.AddString("Field may not be named '" ATHENA_DNA_READER "' or '" ATHENA_DNA_WRITER "'");
                diag.AddSourceRange(clang::CharSourceRange(field->getSourceRange(), true));
                return true;
            }
            clang::QualType qualType = field->getType().getCanonicalType();
            const clang::Type* regType = qualType.getTypePtrOrNull();
            if (regType)
            {
                const clang::CXXRecordDecl* rDecl = regType->getAsCXXRecordDecl();
                if (rDecl)
                {
                    if (!rDecl->getName().compare("Delete"))
                    {
                        const clang::CXXRecordDecl* rParentDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(rDecl->getParent());
                        if (rParentDecl)
                        {
                            std::string parentCheck = rParentDecl->getTypeForDecl()->getCanonicalTypeInternal().getAsString();
                            if (!parentCheck.compare(0, sizeof(ATHENA_DNA_BASETYPE)-1, ATHENA_DNA_BASETYPE))
                                return true;
                        }
                    }
                }
            }
        }

        emitIOFuncs(decl, baseDNA);
        if (isYAML)
            emitYAMLFuncs(decl, baseDNA);

        return true;
    }
};

class ATDNAConsumer : public clang::ASTConsumer
{
    ATDNAEmitVisitor emitVisitor;
    StreamOut& fileOut;
public:
    explicit ATDNAConsumer(clang::ASTContext& context, StreamOut& fo)
    : emitVisitor(context, fo),
      fileOut(fo) {}
    void HandleTranslationUnit(clang::ASTContext& context)
    {
        /* Write file head */
        fileOut << "/* Auto generated atdna implementation */\n"
                   "#include <Athena/Global.hpp>\n"
                   "#include <Athena/IStreamReader.hpp>\n"
                   "#include <Athena/IStreamWriter.hpp>\n\n";
        for (const std::string& inputf : InputFilenames)
            fileOut << "#include \"" << inputf << "\"\n";
        fileOut << "\n";

        /* Emit file */
        emitVisitor.TraverseDecl(context.getTranslationUnitDecl());
    }
};

class ATDNAAction : public clang::ASTFrontendAction
{
public:
    explicit ATDNAAction() {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                          llvm::StringRef /*filename*/)
    {
        StreamOut* fileout;
        if (OutputFilename.size())
            fileout = compiler.createOutputFile(OutputFilename, false, true, "", "", true);
        else
            fileout = compiler.createDefaultOutputFile(false, "a", "cpp");
        AthenaError = compiler.getASTContext().getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error, "Athena error: %0");
        return std::unique_ptr<clang::ASTConsumer>(new ATDNAConsumer(compiler.getASTContext(), *fileout));
    }
};

int main(int argc, const char** argv)
{
    llvm::cl::ParseCommandLineOptions(argc, argv, "Athena DNA Generator");
    if (Help)
        llvm::cl::PrintHelpMessage();

    std::vector<std::string> args = {"clang-tool",
                                     "-fsyntax-only",
                                     "-std=c++11",
                                     "-D__atdna__=1",
                                     "-I" XSTR(INSTALL_PREFIX) "/lib/clang/" CLANG_VERSION_STRING "/include",
                                     "-I" XSTR(INSTALL_PREFIX) "/include/Athena"};
    for (int a=1 ; a<argc ; ++a)
        args.push_back(argv[a]);

    llvm::IntrusiveRefCntPtr<clang::FileManager> fman(new clang::FileManager(clang::FileSystemOptions()));
    clang::tooling::ToolInvocation TI(args, new ATDNAAction, fman.get());
    if (!TI.run())
        return 1;
    
    return 0;
}
