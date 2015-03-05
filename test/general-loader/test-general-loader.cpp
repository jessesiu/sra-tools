/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/

/**
* Unit tests for General Loader
*/

#include <ktst/unit_test.hpp> 

#include <sysalloc.h>

#include "../../tools/general-loader/general-loader.cpp"

#include <klib/printf.h>

#include <kns/adapt.h>

#include <kfs/nullfile.h>
#include <kfs/ramfile.h>
#include <kfs/file.h>

#include <cstring>
#include <stdexcept> 
#include <map>

using namespace std;
using namespace ncbi::NK;

TEST_SUITE(GeneralLoaderTestSuite);

class TestSource
{
public:
    typedef uint32_t TableId;
    typedef uint32_t ColumnId;

public:
    TestSource( const std:: string& p_signature = GeneralLoaderSignatureString, 
                uint32_t p_endness = 1, 
                uint32_t p_version  = 1 )
    :   m_buffer(0), m_bufSize (0)
    {
        strncpy ( m_header . signature, p_signature . c_str(), sizeof m_header . signature );
        m_header . endian                   = p_endness;
        m_header . version                  = p_version;
        m_header . remote_db_name_size      = 0;
        m_header . schema_file_name_size    = 0;
        m_header . schema_spec_size         = 0;
    }
    
    ~TestSource()
    {
        free ( m_buffer );
    }
    
    void SetNames ( const std:: string& p_schemaFile, const std:: string& p_schemaSpec, const std:: string& p_databaseName )
    {
        m_header . remote_db_name_size      = p_databaseName . size();
        m_header . schema_file_name_size    = p_schemaFile . size();
        m_header . schema_spec_size         = p_schemaSpec . size();
        
        m_database      = p_databaseName;
        m_schemaFile    = p_schemaFile;
        m_schemaSpec    = p_schemaSpec;
    }
    
    const std::string & GetDatabaseName() { return m_database; }
    
    const struct KFile * MakeTruncatedSource ( size_t p_size )
    {
        FillBuffer();
        
        const struct KFile * ret;
        if ( KRamFileMakeRead ( & ret, m_buffer, p_size ) != 0 || ret == 0 )
            throw logic_error("TestSource::MakeTruncatedSource KRamFileMakeRead failed");
            
        return ret;
    }
    
    const struct KFile * MakeSource ()
    {
        FillBuffer();
        
        const struct KFile * ret;
        if ( KRamFileMakeRead ( & ret, m_buffer, m_bufSize ) != 0 || ret == 0 )
            throw logic_error("TestSource::MakeTruncatedSource KRamFileMakeRead failed");
            
        return ret;
    }
    
    void OpenStreamEvent ()
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_open_stream ) );
    }
    
    void CloseStreamEvent ()
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_end_stream ) );
    }
    
    void NewTableEvent ( TableId p_id, const char* p_table )
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_new_table, p_id, p_table ) );
    }
    
    void NewColumnEvent ( ColumnId p_columnId, TableId p_tableId, const char* p_column )
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_new_column, p_columnId, p_tableId, p_column ) );
    }
    
    void CellDefaultEvent ( ColumnId p_columnId, const string& p_value )
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_cell_default, p_columnId, 8, p_value . size(), p_value . c_str() ) );
    }
    
    void CellDataEvent ( ColumnId p_columnId, const string& p_value )
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_cell_data, p_columnId, 8, p_value . size(), p_value . c_str() ) );
    }

    void TableCommitEvent ( TableId p_id )
    {
        m_events . push_back ( Event ( GeneralLoader :: evt_next_row, p_id ) );
    }
    
private:    
    void Write( const void * p_data, size_t p_size )
    {
        m_buffer = ( char * ) realloc ( m_buffer, m_bufSize + p_size );
        if ( m_buffer == 0 )
            throw logic_error("TestSource::Write: realloc failed");
            
        memcpy ( m_buffer + m_bufSize, p_data, p_size );
        m_bufSize += p_size;
    }
    
    void Pad ( size_t p_alignment = 4 )
    {
        size_t newSize = ( m_bufSize + p_alignment - 1 ) / p_alignment;
        newSize *= p_alignment;
        if ( m_bufSize != newSize ) 
        {
            m_buffer = ( char * ) realloc ( m_buffer, newSize );
            if ( m_buffer == 0 )
                throw logic_error("TestSource::Write: realloc failed");
            memset ( m_buffer + m_bufSize, 0, newSize - m_bufSize );
            m_bufSize = newSize;
        }
    }
    
    void MakeHeader()
    {
        Write ( & m_header, sizeof m_header );
        Write ( m_database . c_str(), m_header . remote_db_name_size + 1 );
        Write ( m_schemaFile . c_str(), m_header . schema_file_name_size + 1 );
        Write ( m_schemaSpec . c_str(), m_header . schema_spec_size + 1 );
    }

    void FillBuffer()
    {
        MakeHeader();
        Pad ();
        
        for ( Events :: const_iterator it = m_events . begin(); it != m_events . end(); ++it )
        {
            switch ( it -> m_event )
            {
            case GeneralLoader :: evt_open_stream:
            case GeneralLoader :: evt_end_stream:
                {
                    GeneralLoader :: Evt_hdr hdr;
                    hdr . evt   = it -> m_event;
                    hdr . id    = 0;
                    Write ( & hdr, sizeof hdr );
                    break;
                }
            case GeneralLoader :: evt_new_table :
                {
                    GeneralLoader ::  Table_hdr hdr;
                    hdr . evt   = it -> m_event;
                    hdr . id    = it -> m_id1;
                    hdr . table_name_size = it -> m_str . size ();
                    Write ( & hdr, sizeof hdr );
                    Write ( it -> m_str . c_str (), it -> m_str . size () + 1 );
                    break;
                }
            case GeneralLoader :: evt_new_column :
                {
                    GeneralLoader ::  Column_hdr hdr;
                    hdr . evt               = it -> m_event;
                    hdr . id                = it -> m_id1;
                    hdr . table_id          = it -> m_id2;
                    hdr . column_name_size  = it -> m_str . size ();
                    Write ( & hdr, sizeof hdr );
                    Write ( it -> m_str . c_str (), it -> m_str . size () + 1 );
                    break;
                }
            case GeneralLoader :: evt_cell_data :
            case GeneralLoader :: evt_cell_default :
                {
                    GeneralLoader ::  Cell_hdr hdr;
                    hdr . evt               = it -> m_event;
                    hdr . id                = it -> m_id1;
                    hdr . elem_bits         = it -> m_uint1;
                    hdr . elem_count        = it -> m_uint2;
                    Write ( & hdr, sizeof hdr );
                    Write ( it -> m_val . data(), it -> m_val . size() );
                    break;
                }
            case GeneralLoader :: evt_next_row:
                {
                    GeneralLoader :: Evt_hdr hdr;
                    hdr . evt   = it -> m_event;
                    hdr . id    = it -> m_id1;
                    Write ( & hdr, sizeof hdr );
                    break;
                }
            default:
                throw logic_error("TestSource::FillBuffer: event not implemented");
            }
            Pad ();
        }
    }
    
private:    
    struct Event 
    {
        Event ( GeneralLoader :: Evt_id p_event )
        :   m_event ( p_event ),
            m_id1 ( 0 ),
            m_id2 ( 0 ),
            m_uint1 ( 0 ),
            m_uint2 ( 0 )
        {
        }
        
        Event ( GeneralLoader :: Evt_id p_event, uint32_t p_id1 )
        :   m_event ( p_event ),
            m_id1 ( p_id1 ),
            m_id2 ( 0 ),
            m_uint1 ( 0 ),
            m_uint2 ( 0 )
        {
        }
        
        Event ( GeneralLoader :: Evt_id p_event, uint32_t p_id, const char* p_str )
        :   m_event ( p_event ),
            m_id1 ( p_id ),
            m_id2 ( 0 ),
            m_uint1 ( 0 ),
            m_uint2 ( 0 ),
            m_str ( p_str )
        {
        }
        
        Event ( GeneralLoader :: Evt_id p_event, uint32_t p_id1, uint32_t p_id2, const char* p_str )
        :   m_event ( p_event ),
            m_id1 ( p_id1 ),
            m_id2 ( p_id2 ),
            m_uint1 ( 0 ),
            m_uint2 ( 0 ),
            m_str ( p_str )
        {
        }
        
        Event ( GeneralLoader :: Evt_id p_event, uint32_t p_id1, uint32_t p_uint1, uint32_t p_uint2, const void* p_val )
        :   m_event ( p_event ),
            m_id1 ( p_id1 ),
            m_id2 ( 0 ),
            m_uint1 ( p_uint1 ),
            m_uint2 ( p_uint2 )
        {
            size_t bytes = ( p_uint1 * p_uint2 + 7 ) / 8;
            const char* v = ( const char* ) p_val;
            for ( size_t i = 0; i != bytes; ++i )
            {
                m_val . push_back ( v [ i ] );
            }
        }
        
        ~Event()
        {
        }
        
        GeneralLoader :: Evt_id m_event;
        uint32_t                m_id1;
        uint32_t                m_id2;
        uint32_t                m_uint1;
        uint32_t                m_uint2;
        std::string             m_str;
        vector<char>            m_val;
    };
    typedef vector < Event > Events;
    
    string m_database;
    string m_schemaFile;
    string m_schemaSpec;
    
    GeneralLoader :: Header m_header;
    Events                  m_events;
    
    char*   m_buffer;
    size_t  m_bufSize;
};

class GeneralLoaderFixture
{
public:
    GeneralLoaderFixture()
    :   m_db ( 0 ),
        m_cursor ( 0 )
    {
    }
    ~GeneralLoaderFixture()
    {
        RemoveDatabase();
    }
    
    bool Run ( const struct KFile * p_input, rc_t p_rc )
    {
        struct KStream * inStream;
        if ( KStreamFromKFilePair ( & inStream, p_input, 0 ) != 0 )
            throw logic_error("GeneralLoaderFixture::Run KStreamFromKFilePair failed");
            
        GeneralLoader gl ( * inStream );
        rc_t rc = gl.Run();
        bool ret;
        if ( rc == p_rc )
        {
            ret = true;
        }
        else
        {
            char buf[1024];
            string_printf ( buf, sizeof buf, NULL, "Expected rc='%R', actual='%R'", p_rc, rc );
            cerr << buf << endl;
            ret = false;
        }
        
        if ( KStreamRelease ( inStream)  != 0 )
            throw logic_error("GeneralLoaderFixture::Run: KStreamRelease failed");
            
        if ( KFileRelease ( p_input)  != 0 )
            throw logic_error("GeneralLoaderFixture::Run: KFileRelease failed");
            
        return ret;
    }
    
    bool DatabaseExists ()
    {
        if ( m_source . GetDatabaseName() . empty ()  )
        {
            return false;
        }
        KDirectory* wd;
        KDirectoryNativeDir ( & wd );
        bool ret = KDirectoryPathType_v1 ( wd, m_source . GetDatabaseName() . c_str() ) != kptNotFound;
        KDirectoryRelease ( wd );
        return ret;
    }
    
    void RemoveDatabase()
    {
        CloseDatabase();
        if ( ! m_source . GetDatabaseName() . empty ()  )
        {
            KDirectory* wd;
            KDirectoryNativeDir ( & wd );
            KDirectoryRemove ( wd, true, m_source . GetDatabaseName() . c_str() );
            KDirectoryRelease ( wd );
        }
    }
    
    void OpenDatabase()
    {
        CloseDatabase();
        
        VDBManager * vdb;
        if ( VDBManagerMakeUpdate ( & vdb, NULL ) != 0 )
            throw logic_error("GeneralLoaderFixture::OpenDatabase(" + m_source . GetDatabaseName() + "): VDBManagerMakeUpdate failed");
            
        if ( VDBManagerOpenDBUpdate ( vdb, &m_db, NULL, m_source . GetDatabaseName() . c_str() ) != 0 )
            throw logic_error("GeneralLoaderFixture::OpenDatabase(" + m_source . GetDatabaseName() + "): VDBManagerOpenDBUpdate failed");
        
        if ( VDBManagerRelease ( vdb ) != 0 )
            throw logic_error("GeneralLoaderFixture::OpenDatabase(" + m_source . GetDatabaseName() + "): VDBManagerRelease failed");
    }
    void CloseDatabase()
    {
        if ( m_db != 0 )
        {
            VDatabaseRelease ( m_db );
        }
        if ( m_cursor != 0 )
        {
            VCursorRelease ( m_cursor );
        }
    }
    
    void OpenCursor( const char* p_table, const char* p_column )
    {
        OpenDatabase();
        const VTable * tbl;
        if ( VDatabaseOpenTableRead ( m_db, &tbl, p_table ) != 0 )
            throw logic_error(string ( "GeneralLoaderFixture::OpenCursor(" ) + p_table + "): VDatabaseOpenTableRead failed");
        if ( VTableCreateCursorRead ( tbl, & m_cursor ) != 0 )
            throw logic_error(string ( "GeneralLoaderFixture::OpenCursor(" ) + p_table + "): VTableCreateCursorRead failed");
        
        uint32_t idx;
        if ( VCursorAddColumn ( m_cursor, &idx, p_column ) != 0 ) 
            throw logic_error(string ( "GeneralLoaderFixture::OpenCursor(" ) + p_column + "): VCursorAddColumn failed");
        
        if ( VCursorOpen ( m_cursor ) != 0 )
            throw logic_error(string ( "GeneralLoaderFixture::OpenCursor(" ) + p_table + "): VCursorOpen failed");
        
        if ( VTableRelease ( tbl ) != 0 )
            throw logic_error(string ( "GeneralLoaderFixture::OpenCursor(" ) + p_table + "): VTableRelease failed");
    }
    
    std::string GetValue ( const char* p_table, const char* p_column, uint64_t p_row )
    {
        OpenCursor( p_table, p_column ); 
        if ( VCursorSetRowId ( m_cursor, p_row ) ) 
            throw logic_error("GeneralLoaderFixture::GetValue(): VCursorSetRowId failed");
        
        if ( VCursorOpenRow ( m_cursor ) != 0 )
            throw logic_error("GeneralLoaderFixture::GetValue(): VCursorOpenRow failed");
            
        char buf[1024];
        uint32_t num_read;
        if ( VCursorRead ( m_cursor, 1, 8, &buf, sizeof buf, &num_read ) != 0 )
            throw logic_error("GeneralLoaderFixture::GetValue(): VCursorRead failed");
        
        if ( VCursorCloseRow ( m_cursor ) != 0 )
            throw logic_error("GeneralLoaderFixture::GetValue(): VCursorCloseRow failed");

         return string ( buf, num_read );
    }
    
    TestSource      m_source;
    VDatabase *     m_db;
    const VCursor * m_cursor;
};    

FIXTURE_TEST_CASE ( EmptyInput, GeneralLoaderFixture )
{
    const struct KFile * input;
    REQUIRE_RC ( KFileMakeNullRead ( & input ) );
    REQUIRE ( Run ( input, SILENT_RC ( rcNS, rcFile, rcReading, rcTransfer, rcIncomplete ) ) );
}

FIXTURE_TEST_CASE ( ShortInput, GeneralLoaderFixture )
{
    char buffer[10];
    const struct KFile * input;
    REQUIRE_RC ( KRamFileMakeRead ( & input, buffer, sizeof buffer ) );
    
    REQUIRE ( Run ( input, SILENT_RC ( rcNS, rcFile, rcReading, rcTransfer, rcIncomplete ) ) );
}

FIXTURE_TEST_CASE ( BadSignature, GeneralLoaderFixture )
{
    TestSource ts ( "badsigna" );
    REQUIRE ( Run (  ts . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcHeader, rcCorrupt ) ) );
}

FIXTURE_TEST_CASE ( BadEndianness, GeneralLoaderFixture )
{
    TestSource ts ( GeneralLoaderSignatureString, 3 );
    REQUIRE ( Run ( ts . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcFormat, rcInvalid) ) );
}

FIXTURE_TEST_CASE ( ReverseEndianness, GeneralLoaderFixture )
{
    TestSource ts ( GeneralLoaderSignatureString, 0x10000000 );
    REQUIRE ( Run ( ts . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcFormat, rcUnsupported ) ) );
}

FIXTURE_TEST_CASE ( LaterVersion, GeneralLoaderFixture )
{
    TestSource ts ( GeneralLoaderSignatureString, 1, 2 );
    REQUIRE ( Run ( ts . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcHeader, rcBadVersion ) ) );
}

FIXTURE_TEST_CASE ( TruncatedMetadata , GeneralLoaderFixture )
{   // not enough data to read the fixed part of the metadata 
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    REQUIRE ( Run ( m_source . MakeTruncatedSource ( sizeof ( GeneralLoader :: Header ) + 8 ), 
              SILENT_RC ( rcNS, rcFile, rcReading, rcTransfer, rcIncomplete ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadSchemaFileName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "this file should not exist", "someSchemaName", "database" );
    m_source . OpenStreamEvent ();
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcFS, rcDirectory, rcOpening, rcPath, rcNotFound ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadSchemaName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "bad schema name", "database" );
    m_source . OpenStreamEvent ();
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcVDB, rcMgr, rcCreating, rcSchema, rcNotFound ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadDatabaseName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", "" );
    m_source . OpenStreamEvent ();
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcDB, rcMgr, rcCreating, rcPath, rcIncorrect ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadTableName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "nosuchtable" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcVDB, rcDatabase, rcCreating, rcSchema, rcNotFound ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( DuplicateTableId, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "REFERENCE" );
    m_source . NewTableEvent ( 1, "SEQUENCE" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcTable, rcExists ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadColumnName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "REFERENCE" );
    m_source . NewColumnEvent ( 1, 1, "nosuchcolumn" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcVDB, rcCursor, rcUpdating, rcColumn, rcNotFound ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( BadTableId, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "REFERENCE" );
    m_source . NewColumnEvent ( 1, 2, "SPOT_GROUP" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcTable, rcInvalid ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( DuplicateColumnName, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "REFERENCE" );
    m_source . NewColumnEvent ( 1, 1, "SPOT_GROUP" );
    m_source . NewColumnEvent ( 2, 1, "SPOT_GROUP" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcVDB, rcCursor, rcUpdating, rcColumn, rcExists ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( DuplicateColumnId, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . NewTableEvent ( 1, "REFERENCE" );
    m_source . NewColumnEvent ( 1, 1, "SPOT_GROUP" );
    m_source . NewColumnEvent ( 1, 1, "NAME" );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcColumn, rcExists ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( NoOpenStreamEvent, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcNS, rcFile, rcReading, rcTransfer, rcIncomplete ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( NoCloseStreamEvent, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . OpenStreamEvent();
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcNS, rcFile, rcReading, rcTransfer, rcIncomplete ) ) );
    REQUIRE ( ! DatabaseExists() );
}

FIXTURE_TEST_CASE ( NoColumns, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    m_source . OpenStreamEvent();
    m_source . CloseStreamEvent();
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    // make sure database exists and is valid
    REQUIRE ( DatabaseExists() );
    OpenDatabase (); // did not throw => opened successfully
}

FIXTURE_TEST_CASE ( NoData, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 2, tableName ); // ids do not have to consecutive
    m_source . NewColumnEvent ( 300, 2, columnName );   
    m_source . OpenStreamEvent();
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    // validate the database
    OpenCursor( tableName, columnName ); 
    uint64_t count;
    REQUIRE_RC ( VCursorIdRange ( m_cursor, 1, NULL, &count ) );
    REQUIRE_EQ ( (uint64_t)0, count );
}

FIXTURE_TEST_CASE ( Chunk_BadColumnId, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 1, tableName );
    m_source . NewColumnEvent ( 1, 1, columnName );
    m_source . OpenStreamEvent();
    m_source . CellDataEvent( /*bad*/2, string() );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcColumn, rcNotFound ) ) );
}

FIXTURE_TEST_CASE ( WriteNoCommit, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 1, tableName ); 
    m_source . NewColumnEvent ( 1, 1, columnName );
    m_source . OpenStreamEvent();
    string value = "a single character string cell";
    m_source . CellDataEvent( 1, value );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    // validate the database
    OpenCursor( tableName, columnName ); 
    uint64_t count;
    REQUIRE_RC ( VCursorIdRange ( m_cursor, 1, NULL, &count ) );
    REQUIRE_EQ ( (uint64_t)0, count );
}

FIXTURE_TEST_CASE ( CommitBadTableId, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    m_source . NewTableEvent ( 1, tableName ); 
    m_source . OpenStreamEvent();
    m_source . TableCommitEvent ( /*bad*/2 );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), SILENT_RC ( rcExe, rcFile, rcReading, rcTable, rcNotFound ) ) );
}    

FIXTURE_TEST_CASE ( OneColumnOneCellOneChunk, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 1, tableName ); 
    m_source . NewColumnEvent ( 1, 1, columnName );
    m_source . OpenStreamEvent();
    string value = "a single character string cell";
    m_source . CellDataEvent( 1, value );
    m_source . TableCommitEvent ( 1 );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    REQUIRE_EQ ( value, GetValue ( tableName, columnName, 1 ) ); 
}

FIXTURE_TEST_CASE ( OneColumnOneCellManyChunks, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 1, tableName ); 
    m_source . NewColumnEvent ( 1, 1, columnName );
    m_source . OpenStreamEvent();
    string value1 = "first";
    m_source . CellDataEvent( 1, value1 );
    string value2 = "second!";
    m_source . CellDataEvent( 1, value2 );
    string value3 = "third!!!";
    m_source . CellDataEvent( 1, value3 );
    m_source . TableCommitEvent ( 1 );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    REQUIRE_EQ ( value1 + value2 + value3, GetValue ( tableName, columnName, 1 ) ); 
}

FIXTURE_TEST_CASE ( OneColumnDefaultNoWrite, GeneralLoaderFixture )
{   
    m_source . SetNames ( "align/align.vschema", "NCBI:align:db:alignment_sorted", GetName() );
    const char* tableName = "REFERENCE";
    const char* columnName = "SPOT_GROUP";
    
    m_source . NewTableEvent ( 1, tableName ); 
    m_source . NewColumnEvent ( 1, 1, columnName );
    m_source . OpenStreamEvent();
    string value = "this be my default";
    m_source . CellDefaultEvent( 1, value );
    // no WriteEvent
    m_source . TableCommitEvent ( 1 );
    m_source . CloseStreamEvent();
    
    REQUIRE ( Run ( m_source . MakeSource (), 0 ) );
    
    // validate the database
    REQUIRE_EQ ( value, GetValue ( tableName, columnName, 1 ) ); 
}

//TODO:
//  one table, one column, set-default, write (not a default), commit, close
//  one table, one column, set-default, (no write), commit, set-default (different), (no write),  commit, close
//  one table, one column, write a chunk, set-default, write another chunk, commit, (no write), commit, close
//
//  one table, multiple columns, write all cells, commit, close
//  one table, multiple columns, write some cells, commit, error (no defaults)
//  one table, multiple columns, set defaults for some cells, write others, commit, close
//  one table, multiple columns, set defaults for some cells, write others (some overwrite defaults), commit, close
//
//  multiple tables, multiple columns, write all cells, commit, write all cells, commit, close

//////////////////////////////////////////// Main
extern "C"
{

#include <kapp/args.h>
#include <kfg/config.h>

ver_t CC KAppVersion ( void )
{
    return 0x1000000;
}
rc_t CC UsageSummary (const char * progname)
{
    return 0;
}

rc_t CC Usage ( const Args * args )
{
    return 0;
}

const char UsageDefaultName[] = "test-general-loader";

rc_t CC KMain ( int argc, char *argv [] )
{
    KConfigDisableUserSettings();
    rc_t rc=GeneralLoaderTestSuite(argc, argv);
    return rc;
}

}  
