/*******************************************************

   CoolReader Engine

   lvxml.cpp:  XML parser implementation

   (c) Vadim Lopatin, 2000-2006
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

#include "../include/lvxml.h"
#include "../include/crtxtenc.h"

#define BUF_SIZE_INCREMENT 4096
#define MIN_BUF_DATA_SIZE 2048
#define CP_AUTODETECT_BUF_SIZE 0x10000


/// virtual destructor
LVFileFormatParser::~LVFileFormatParser()
{
}

LVTextFileBase::LVTextFileBase( LVStreamRef stream )
    : m_stream(stream)
    , m_buf(NULL)
    , m_buf_size(0)
    , m_stream_size(0)
    , m_buf_len(0)
    , m_buf_pos(0)
    , m_buf_fpos(0)
    , m_enc_type( ce_8bit_cp )
    , m_conv_table(NULL)
{
    m_stream_size = stream->GetSize();
}

/// destructor
LVTextFileBase::~LVTextFileBase()
{
    if (m_buf)
        free( m_buf );
}

lChar16 LVTextFileBase::ReadChar()
{
    lChar16 ch = m_buf[m_buf_pos++];
    switch ( m_enc_type ) {
    case ce_8bit_cp:
    case ce_utf8:
        if ( (ch & 0x80) == 0 )
            return ch;
        if (m_conv_table)
        {
            return m_conv_table[ch&0x7F];
        }
        else
        {
            // support only 11 and 16 bit UTF8 chars
            if ( (ch & 0xE0) == 0xC0 )
            {
                // 11 bits
                return ((lUInt16)(ch&0x1F)<<6)
                    | ((lUInt16)m_buf[m_buf_pos++]&0x3F);
            } else {
                // 16 bits
                ch = (ch&0x0F);
                lUInt16 ch2 = (m_buf[m_buf_pos++]&0x3F);
                lUInt16 ch3 = (m_buf[m_buf_pos++]&0x3F);
                return (ch<<12) | (ch2<<6) | ch3;
            }
        }
    case ce_utf16_be:
        {
            lChar16 ch2 = m_buf[m_buf_pos++];
            return (ch << 8) || ch2;
        }
    case ce_utf16_le:
        {
            lChar16 ch2 = m_buf[m_buf_pos++];
            return (ch2 << 8) || ch;
        }
    case ce_utf32_be:
        // support 16 bits only
        m_buf_pos++;
        {
            lChar16 ch3 = m_buf[m_buf_pos++];
            lChar16 ch4 = m_buf[m_buf_pos++];
            return (ch3 << 8) || ch4;
        }
    case ce_utf32_le:
        // support 16 bits only
        {
            lChar16 ch2 = m_buf[m_buf_pos++];
            m_buf_pos+=2;
            return (ch << 8) || ch2;
        }
    default:
        return 0;
    }
}

/// tries to autodetect text encoding
bool LVTextFileBase::AutodetectEncoding()
{
    char enc_name[32];
    char lang_name[32];
    lvpos_t oldpos = m_stream->GetPos();
    unsigned sz = CP_AUTODETECT_BUF_SIZE;
    m_stream->SetPos( 0 );
    if ( sz>m_stream->GetSize() )
        sz = m_stream->GetSize();
    if ( sz < 40 )
        return false;
    unsigned char * buf = new unsigned char[ sz ];
    lvsize_t bytesRead = 0;
    m_stream->Read( buf, sz, &bytesRead );

    int res = AutodetectCodePage( buf, sz, enc_name, lang_name );
    m_lang_name = lString16( lang_name );
    SetCharset( lString16( enc_name ).c_str() );

    // restore state
    delete buf;
    m_stream->SetPos( oldpos );
    return true;
}

/// seek to specified stream position
bool LVTextFileBase::Seek( lvpos_t pos, int bytesToPrefetch )
{
    if ( pos >= m_buf_fpos && pos+bytesToPrefetch <= (m_buf_fpos+m_buf_len) ) {
        m_buf_pos = (pos - m_buf_fpos);
        return true;
    }
    if ( pos<0 || pos>=m_stream_size )
        return false;
    unsigned bytesToRead = (bytesToPrefetch > m_buf_size) ? bytesToPrefetch : m_buf_size;
    if ( bytesToRead < BUF_SIZE_INCREMENT )
        bytesToRead = BUF_SIZE_INCREMENT;
    if ( bytesToRead > (m_stream_size - pos) )
        bytesToRead = (m_stream_size - pos);
    if ( (unsigned)m_buf_size < bytesToRead ) {
        m_buf_size = bytesToRead;
        m_buf = (lUInt8 *)realloc( m_buf, m_buf_size + 16 );
    }
    m_buf_fpos = pos;
    m_buf_pos = 0;
    m_buf_len = m_buf_size;
    // TODO: add error handing
    m_stream->SetPos( m_buf_fpos );
    lvsize_t bytesRead = 0;
    m_stream->Read( m_buf, bytesToRead, &bytesRead );
    return true;
}

/// reads specified number of bytes, converts to characters and saves to buffer
int LVTextFileBase::ReadTextBytes( lvpos_t pos, int bytesToRead, lChar16 * buf, int buf_size)
{
    if ( !Seek( pos, bytesToRead ) )
        return 0;
    int chcount = 0;
    int max_pos = m_buf_pos + bytesToRead;
    if ( max_pos > m_buf_len )
        max_pos = m_buf_len;
    while ( m_buf_pos<max_pos && chcount < buf_size ) {
        *buf++ = ReadChar();
        chcount++;
    }
    return chcount;
}

/// reads specified number of characters and saves to buffer
int LVTextFileBase::ReadTextChars( lvpos_t pos, int charsToRead, lChar16 * buf, int buf_size)
{
    if ( !Seek( pos, charsToRead*4 ) )
        return 0;
    int chcount = 0;
    if ( buf_size > charsToRead )
        buf_size = charsToRead;
    while ( m_buf_pos<m_buf_len && chcount < buf_size ) {
        *buf++ = ReadChar();
        chcount++;
    }
    return chcount;
}

bool LVTextFileBase::FillBuffer( int bytesToRead )
{
    lvoffset_t bytesleft = (lvoffset_t) (m_stream_size - (m_buf_fpos+m_buf_len));
    if (bytesleft<=0)
        return false;
    if (bytesToRead > bytesleft)
        bytesToRead = (int)bytesleft;
    int space = m_buf_size - m_buf_len;
    if (space < bytesToRead)
    {
        if ( m_buf_pos>bytesToRead || m_buf_pos>((m_buf_len*3)>>2) )
        {
            // just move
            int sz = (int)(m_buf_len -  m_buf_pos);
            for (int i=0; i<sz; i++)
            {
                m_buf[i] = m_buf[i+m_buf_pos];
            }
            m_buf_len = sz;
            m_buf_fpos += m_buf_pos;
            m_buf_pos = 0;
            space = m_buf_size - m_buf_len;
        }
        if (space < bytesToRead)
        {
            m_buf_size = m_buf_size + (bytesToRead - space + BUF_SIZE_INCREMENT);
            m_buf = (lUInt8 *)realloc( m_buf, m_buf_size + 16 );
        }
    }
    lvsize_t n = 0;
    m_stream->Read(m_buf+m_buf_len, bytesToRead, &n);
    m_buf_len += (int)n;
    return (n>0);
}

void LVTextFileBase::Reset()
{
    m_stream->SetPos(0);
    m_buf_fpos = 0;
    m_buf_pos = 0;
    m_buf_len = 0;
    m_stream_size = m_stream->GetSize();
}

void LVTextFileBase::SetCharset( const lChar16 * name )
{
    m_encoding_name = lString16( name );
    if ( name == L"utf-8" ) {
        m_enc_type = ce_utf8;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-16" ) {
        m_enc_type = ce_utf16_le;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-16le" ) {
        m_enc_type = ce_utf16_le;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-16be" ) {
        m_enc_type = ce_utf16_be;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-32" ) {
        m_enc_type = ce_utf32_le;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-32le" ) {
        m_enc_type = ce_utf32_le;
        SetCharsetTable( NULL );
    } else if ( name == L"utf-32be" ) {
        m_enc_type = ce_utf32_be;
        SetCharsetTable( NULL );
    } else {
        m_enc_type = ce_8bit_cp;
        const lChar16 * table = GetCharsetByte2UnicodeTable( name );
        if ( table )
            SetCharsetTable( table );
    }
}

void LVTextFileBase::SetCharsetTable( const lChar16 * table )
{
    if (!table)
    {
        if (m_conv_table)
        {
            delete m_conv_table;
            m_conv_table = NULL;
        }
        return;
    }
    m_enc_type = ce_8bit_cp;
    if (!m_conv_table)
        m_conv_table = new lChar16[128];
    lStr_memcpy( m_conv_table, table, 128 );
}


static const lChar16 * heading_volume[] = {
    L"volume",
    L"vol",
    L"\x0442\x043e\x043c", // tom
    NULL
};

static const lChar16 * heading_part[] = {
    L"part",
    L"\x0447\x0430\x0441\x0442\x044c", // chast'
    NULL
};

static const lChar16 * heading_chapter[] = {
    L"chapter",
    L"\x0433\x043B\x0430\x0432\x0430", // glava
    NULL
};

static bool startsWithOneOf( lString16 str, const lChar16 * list[] )
{
    str.lowercase();
    const lChar16 * p = str.c_str();
    for ( int i=0; list[i]; i++ ) {
        const lChar16 * q = list[i];
        int j=0;
        for ( ; q[j]; j++ ) {
            if ( !p[j] ) {
                return (!q[j] || q[j]==' ');
            }
            if ( p[j] != q[j] )
                break;
        }
        if ( !q[j] )
            return true;
    }
    return false;
}

int DetectHeadingLevelByText( const lString16 & str )
{
    if ( str.empty() )
        return 0;
    if ( startsWithOneOf( str, heading_volume ) )
        return 1;
    if ( startsWithOneOf( str, heading_part ) )
        return 2;
    if ( startsWithOneOf( str, heading_chapter ) )
        return 3;
    lChar16 ch = str[0];
    if ( ch>='0' && ch<='9' ) {
        unsigned i;
        int point_count = 0;
        for ( i=1; i<str.length(); i++ ) {
            ch = str[i];
            if ( ch>='0' && ch<='9' )
                continue;
            if ( ch!='.' )
                return 0;
            point_count++;
        }
        return (str.length()<80) ? 4+point_count : 0;
    }
    return 0;
}

class LVTextFileLine
{
public:
    lvpos_t fpos;   // position of line in file
    lvsize_t fsize;  // size of data in file
    lUInt32 flags;  // flags. 1=eoln
    lString16 text; // line text
    lUInt16 lpos;   // left non-space char position
    lUInt16 rpos;   // right non-space char posision + 1
    LVTextFileLine( LVTextFileBase * file, int maxsize )
    : lpos(0), rpos(0)
    {
        text = file->ReadLine( maxsize, fpos, fsize, flags );
        if ( !text.empty() ) {
            const lChar16 * s = text.c_str();
            for ( int p=0; *s; s++ ) {
                if ( *s == '\t' ) {
                    p = (p + 8)%8;
                } else {
                    if ( *s != ' ' ) {
                        if ( rpos==0 )
                            lpos = p;
                        rpos = p + 1;
                    }
                    p++;
                }
            }
        }
    }
};

class LVTextLineQueue : public LVPtrVector<LVTextFileLine>
{
private:
    LVTextFileBase * file;
    int first_line_index;
    int maxLineSize;
    lString16 authorFirstName;
    lString16 authorLastName;
    lString16 bookTitle;
    lString16 seriesName;
    lString16 seriesNumber;
    int formatFlags;
    int min_left;
    int max_right;
    int avg_left;
    int avg_right;
    int paraCount;

    enum {
        tftParaPerLine = 1,
        tftParaIdents  = 2,
        tftEmptyLineDelimPara = 4,
        tftCenteredHeaders = 8,
        tftEmptyLineDelimHeaders = 16,
    } formatFlags_t;
public:
    LVTextLineQueue( LVTextFileBase * f, int maxLineLen )
    : file(f), first_line_index(0), maxLineSize(maxLineLen)
    {
        min_left = -1;
        max_right = -1;
        avg_left = 0;
        avg_right = 0;
        paraCount = 0;
    }
    // get index of first line of queue
    int  GetFirstLineIndex() { return first_line_index; }
    // get line count read from file. Use length() instead to get count of lines queued.
    int  GetLineCount() { return first_line_index + length(); }
    // get line by line file index
    LVTextFileLine * GetLine( int index )
    {
        return get(index - first_line_index);
    }
    // remove lines from head of queue
    void RemoveLines( int lineCount )
    {
        if ( lineCount>length() )
            lineCount = length();
        erase( 0, lineCount );
        first_line_index += lineCount;
    }
    // read lines and place to tail of queue
    bool ReadLines( int lineCount )
    {
        for ( int i=0; i<lineCount; i++ ) {
            if ( file->Eof() ) {
                if ( i==0 )
                    return false;
            }
            add( new LVTextFileLine( file, maxLineSize ) );
        }
        return true;
    }
    /// checks text format options
    void detectFormatFlags()
    {
        formatFlags = tftParaPerLine | tftEmptyLineDelimHeaders; // default format
        if ( length()<10 )
            return;
        formatFlags = 0;
        int avg_center = 0;
        int empty_lines = 0;
        int ident_lines = 0;
        min_left = -1;
        max_right = -1;
        avg_left = 0;
        avg_right = 0;
        int i;
        for ( i=0; i<length(); i++ ) {
            LVTextFileLine * line = get(i);
            if ( line->lpos == line->rpos ) {
                empty_lines++;
            } else {
                if ( i==0 || line->lpos<min_left )
                    min_left = line->lpos;
                if ( i==0 || line->rpos>max_right )
                    max_right = line->rpos;
                avg_left += line->lpos;
                avg_right += line->rpos;
            }
        }
        for ( i=0; i<length(); i++ ) {
            LVTextFileLine * line = get(i);
            if ( line->lpos > min_left )
                ident_lines++;
        }
        int non_empty_lines = length() - empty_lines;
        if ( non_empty_lines < 10 )
            return;
        avg_left /= length();
        avg_right /= length();
        avg_center = (avg_left + avg_right) / 2;
        if ( avg_right >= 80 )
            return;
        formatFlags = 0;
        int ident_lines_percent = ident_lines * 100 / length();
        int empty_lines_precent = empty_lines * 100 / length();
        if ( empty_lines_precent > 5 )
            formatFlags |= tftEmptyLineDelimPara;
        if ( ident_lines_percent > 5 )
            formatFlags |= tftParaIdents;
        if ( !formatFlags ) {
            formatFlags = tftParaPerLine | tftEmptyLineDelimHeaders; // default format
            return;
        }

    }
    /// check beginning of file for book title, author and series
    bool DetectBookDescription(LVXMLParserCallback * callback)
    {
        int necount = 0;
        lString16 s[3];
        unsigned i;
        for ( i=0; i<(unsigned)length() && necount<2; i++ ) {
            LVTextFileLine * item = get(i);
            if ( item->rpos>item->lpos ) {
                lString16 str = item->text;
                str.trimDoubleSpaces(false, false, true);
                if ( !str.empty() ) {
                    s[necount] = str;
                    necount++;
                }
            }
        }
        //update book description
        if ( i==0 ) {
            bookTitle = L"no name";
        } else {
            bookTitle = s[1];
        }
        lString16Collection author_list;
        author_list.parse( s[0], ',', true );
        for ( i=0; i<author_list.length(); i++ ) {
            lString16Collection name_list;
            name_list.parse( author_list[i], ' ', true );
            if ( name_list.length()>0 ) {
                lString16 firstName = name_list[0];
                lString16 lastName;
                lString16 middleName;
                if ( name_list.length() == 2 ) {
                    lastName = name_list[1];
                } else if ( name_list.length()>2 ) {
                    middleName = name_list[1];
                    lastName = name_list[2];
                }
                callback->OnTagOpen( NULL, L"author" );
                  callback->OnTagOpen( NULL, L"first-name" );
                    if ( !firstName.empty() ) 
                        callback->OnText( firstName.c_str(), firstName.length(), 0, 0, TXTFLG_TRIM|TXTFLG_TRIM_REMOVE_EOL_HYPHENS );
                  callback->OnTagClose( NULL, L"first-name" );
                  callback->OnTagOpen( NULL, L"middle-name" );
                    if ( !middleName.empty() )
                        callback->OnText( middleName.c_str(), middleName.length(), 0, 0, TXTFLG_TRIM|TXTFLG_TRIM_REMOVE_EOL_HYPHENS );
                  callback->OnTagClose( NULL, L"middle-name" );
                  callback->OnTagOpen( NULL, L"last-name" );
                    if ( !lastName.empty() )
                        callback->OnText( lastName.c_str(), lastName.length(), 0, 0, TXTFLG_TRIM|TXTFLG_TRIM_REMOVE_EOL_HYPHENS );
                  callback->OnTagClose( NULL, L"last-name" );
                callback->OnTagClose( NULL, L"author" );
            }
        }
        callback->OnTagOpen( NULL, L"book-title" );
            if ( !bookTitle.empty() )
                callback->OnText( bookTitle.c_str(), bookTitle.length(), 0, 0, 0 );
        callback->OnTagClose( NULL, L"book-title" );
        if ( !seriesName.empty() || !seriesNumber.empty() ) {
            callback->OnTagOpen( NULL, L"sequence" );
            if ( !seriesName.empty() )
                callback->OnAttribute( NULL, L"name", seriesName.c_str() );
            if ( !seriesNumber.empty() )
                callback->OnAttribute( NULL, L"number", seriesNumber.c_str() );
            callback->OnTagClose( NULL, L"sequence" );
        }
        return true;
    }
    /// add one paragraph
    void AddPara( int startline, int endline, LVXMLParserCallback * callback )
    {
        lString16 str;
        lvpos_t pos = 0;
        lvsize_t sz = 0;
        for ( int i=startline; i<=endline; i++ ) {
            LVTextFileLine * item = get(i);
            if ( i==startline )
                pos = item->fpos;
            sz = (item->fpos + item->fsize) - pos;
            str += item->text + L"\n";
        }
        str.trimDoubleSpaces(false, false, true);
        bool isHeader = str.length()<4 || (paraCount<2 && str.length()<50);
        int hlevel = DetectHeadingLevelByText( str );
        if ( hlevel>0 )
            isHeader = true;
        if ( !str.empty() ) {
            if ( isHeader )
                callback->OnTagOpen( NULL, L"title" );
            callback->OnTagOpen( NULL, L"p" );
               callback->OnText( str.c_str(), str.length(), pos, sz, 
                   TXTFLG_TRIM | TXTFLG_TRIM_REMOVE_EOL_HYPHENS );
            callback->OnTagClose( NULL, L"p" );
            if ( isHeader )
                callback->OnTagClose( NULL, L"title" );
            paraCount++;
        } else {
            if ( !(formatFlags & tftEmptyLineDelimPara) ) {
                callback->OnTagOpen( NULL, L"empty-line" );
                callback->OnTagClose( NULL, L"empty-line" );
            }
        }
    }
    /// one line per paragraph
    bool DoParaPerLineImport(LVXMLParserCallback * callback)
    {
        do {
            for ( int i=0; i<length(); i++ ) {
                AddPara( i, i, callback );
            }
            RemoveLines( length() );
        } while ( ReadLines( 100 ) );
        return true;
    }
#define MAX_PARA_LINES 30
#define MAX_BUF_LINES  200
    /// delimited by first line ident
    bool DoIdentParaImport(LVXMLParserCallback * callback)
    {
        int pos = 0;
        for ( ;; ) {
            if ( length()-pos <= MAX_PARA_LINES ) {
                if ( pos )
                    RemoveLines( pos );
                ReadLines( MAX_BUF_LINES );
                pos = 0;
            }
            if ( pos>=length() )
                break;
            int i=pos+1;
            if ( pos>=length() || DetectHeadingLevelByText( get(pos)->text )==0 ) {
                for ( ; i<length() && i<pos+MAX_PARA_LINES; i++ ) {
                    LVTextFileLine * item = get(i);
                    if ( item->lpos>min_left ) {
                        // ident
                        break;
                    }
                }
            }
            AddPara( pos, i-1, callback );
            pos = i;
        }
        return true;
    }
    /// delimited by empty lines
    bool DoEmptyLineParaImport(LVXMLParserCallback * callback)
    {
        int pos = 0;
        for ( ;; ) {
            if ( length()-pos <= MAX_PARA_LINES ) {
                if ( pos )
                    RemoveLines( pos );
                ReadLines( MAX_BUF_LINES );
                pos = 0;
            }
            if ( pos>=length() )
                break;
            int i=pos;
            if ( pos>=length() || DetectHeadingLevelByText( get(pos)->text )==0 ) {
                for ( ; i<length() && i<pos+MAX_PARA_LINES; i++ ) {
                    LVTextFileLine * item = get(i);
                    if ( item->lpos==item->lpos ) {
                        // empty line
                        break;
                    }
                }
            }
            AddPara( pos, i, callback );
            pos = i+1;
        }
        return true;
    }
    /// import document body
    bool DoTextImport(LVXMLParserCallback * callback)
    {
        if ( formatFlags & tftParaIdents )
            return DoIdentParaImport( callback );
        else if ( formatFlags & tftEmptyLineDelimPara )
            return DoEmptyLineParaImport( callback );
        else
            return DoParaPerLineImport( callback );
    }
};

/// reads next text line, tells file position and size of line, sets EOL flag
lString16 LVTextFileBase::ReadLine( int maxLineSize, lvpos_t & fpos, lvsize_t & fsize, lUInt32 & flags )
{
    fpos = m_buf_fpos + m_buf_pos;
    fsize = 0;
    flags = 0;

    lString16 res;
    res.reserve( 80 );
    FillBuffer( maxLineSize*3 );

    lvpos_t last_space_fpos = 0;
    int last_space_chpos = -1; 
    lChar16 ch = 0;
    while ( res.length()<(unsigned)maxLineSize ) {
        if ( Eof() ) {
            // EOF: treat as EOLN
            last_space_fpos = m_buf_fpos + m_buf_pos;
            last_space_chpos = res.length();
            flags |= 1; // EOLN flag
            break;
        }
        ch = ReadChar();
        if ( ch==0xFEFF && fpos==0 && res.empty() ) {
            fpos = m_buf_fpos + m_buf_pos;
        } else if ( ch!='\r' && ch!='\n' ) {
            res.append( 1, ch );
            if ( ch==' ' || ch=='\t' ) {
                last_space_fpos = m_buf_fpos + m_buf_pos;
                last_space_chpos = res.length();
            }
        } else {
            // eoln
            lvpos_t prev_pos = m_buf_pos;
            last_space_fpos = m_buf_fpos + m_buf_pos;
            last_space_chpos = res.length();
            if ( !Eof() ) {
                lChar16 ch2 = ReadChar();
                if ( ch2!=ch && (ch2=='\r' || ch2=='\n') ) {
                    last_space_fpos = m_buf_fpos + m_buf_pos;
                } else {
                    m_buf_pos = prev_pos;
                }
            }
            flags |= 1; // EOLN flag
            break;
        }
    }
    // now if flags==0, maximum line len reached
    if ( !flags && last_space_chpos == -1 ) {
        // long line w/o spaces
        last_space_fpos = m_buf_fpos + m_buf_pos;
        last_space_chpos = res.length();
    }

    m_buf_pos = (last_space_fpos - m_buf_fpos); // rollback to end of line
    fsize = last_space_fpos - fpos; // length in bytes
    if ( (unsigned)last_space_chpos>res.length() ) {
        res.erase( last_space_chpos, res.length()-last_space_chpos );
    }

    res.pack();
    return res;
}

//==================================================
// Text file parser

/// constructor
LVTextParser::LVTextParser( LVStreamRef stream, LVXMLParserCallback * callback )
    : LVTextFileBase(stream)
    , m_callback(callback)
{
}

/// descructor
LVTextParser::~LVTextParser()
{
}

/// returns true if format is recognized by parser
bool LVTextParser::CheckFormat()
{
    Reset();
    // encoding test
    if ( !AutodetectEncoding() )
        return false;
    #define TEXT_PARSER_DETECT_SIZE 16384
    Reset();
    lChar16 * chbuf = new lChar16[TEXT_PARSER_DETECT_SIZE];
    FillBuffer( TEXT_PARSER_DETECT_SIZE );
    int charsDecoded = ReadTextBytes( 0, TEXT_PARSER_DETECT_SIZE, chbuf+m_buf_pos, m_buf_len-m_buf_pos );
    bool res = false;
    if ( charsDecoded > 100 ) {
        int illegal_char_count = 0;
        int crlf_count = 0;
        int space_count = 0;
        for ( int i=0; i<charsDecoded; i++ ) {
            if ( chbuf[i]<=32 ) {
                switch( chbuf[i] ) {
                case ' ':
                case '\t':
                    space_count++;
                    break;
                case 10:
                case 13:
                    crlf_count++;
                    break;
                case 12:
                //case 9:
                case 8:
                case 7:
                case 30:
                    break;
                default:
                    illegal_char_count++;
                }
            }
        }
        if ( illegal_char_count==0 && space_count>=charsDecoded/16 )
            res = true;
    }
    delete chbuf;
    Reset();
    return res;
}

/// parses input stream
bool LVTextParser::Parse()
{
    LVTextLineQueue queue( this, 1000 );
    queue.ReadLines( 200 );
    queue.detectFormatFlags();
    // make fb2 document structure
    m_callback->OnTagOpen( NULL, L"?xml" );
    m_callback->OnAttribute( NULL, L"version", L"1.0" );
    //m_callback->OnAttribute( NULL, L"encoding", L"UTF-8" );
    m_callback->OnTagClose( NULL, L"?xml" );
    m_callback->OnTagOpen( NULL, L"FictionBook" );
      // DESCRIPTION
      m_callback->OnTagOpen( NULL, L"description" );
        m_callback->OnTagOpen( NULL, L"title-info" );
          queue.DetectBookDescription( m_callback );
        m_callback->OnTagOpen( NULL, L"title-info" );
      m_callback->OnTagClose( NULL, L"description" );
      // BODY
      m_callback->OnTagOpen( NULL, L"body" );
        m_callback->OnTagOpen( NULL, L"section" );
          // process text
          queue.DoTextImport( m_callback );
        m_callback->OnTagClose( NULL, L"section" );
      m_callback->OnTagClose( NULL, L"body" );
    m_callback->OnTagClose( NULL, L"FictionBook" );
    return true;
}


/*******************************************************************************/
// LVXMLTextCache
/*******************************************************************************/

/// parses input stream
bool LVXMLTextCache::Parse()
{
    return true;
}

/// returns true if format is recognized by parser
bool LVXMLTextCache::CheckFormat()
{
    return true;
}

LVXMLTextCache::~LVXMLTextCache()
{
    while (m_head)
    {
        cache_item * ptr = m_head;
        m_head = m_head->next;
        delete ptr;
    }
}

void LVXMLTextCache::addItem( lString16 & str )
{
    cleanOldItems( str.length() );
    cache_item * ptr = new cache_item( str );
    ptr->next = m_head;
    m_head = ptr;
}

void LVXMLTextCache::cleanOldItems( lUInt32 newItemChars )
{
    lUInt32 sum_chars = newItemChars;
    cache_item * ptr = m_head, * prevptr = NULL;
    for ( lUInt32 n = 1; ptr; ptr = ptr->next, n++ )
    {
        sum_chars += ptr->text.length();
        if (sum_chars > m_max_charcount || n>=m_max_itemcount )
        {
            // remove tail
            for (cache_item * p = ptr; p; )
            {
                cache_item * tmp = p;
                p = p->next;
                delete tmp;
            }
            if (prevptr)
                prevptr->next = NULL;
            else
                m_head = NULL;
            return;
        }
        prevptr = ptr;
    }
}

lString16 LVXMLTextCache::getText( lUInt32 pos, lUInt32 size, lUInt32 flags )
{
    // TRY TO SEARCH IN CACHE
    cache_item * ptr = m_head, * prevptr = NULL;
    for ( ;ptr ;ptr = ptr->next )
    {
        if (ptr->pos == pos)
        {
            // move to top
            if (prevptr)
            {
                prevptr->next = ptr->next;
                ptr->next = m_head;
                m_head = ptr;
            }
            return ptr->text;
        }
    }
    // NO CACHE RECORD FOUND
    // read new pme
    lString16 text;
    text.reserve(size);
    text.append(size, ' ');
    lChar16 * buf = text.modify();
    unsigned chcount = (unsigned)ReadTextBytes( pos, size, buf, size );
    if ( chcount<size )
        text.erase( chcount, text.length()-chcount );
    if ( flags & TXTFLG_TRIM ) {
        text.trimDoubleSpaces( 
            (flags & TXTFLG_TRIM_ALLOW_START_SPACE)?true:false, 
            (flags & TXTFLG_TRIM_ALLOW_END_SPACE)?true:false, 
            (flags & TXTFLG_TRIM_REMOVE_EOL_HYPHENS)?true:false );
    }
    // ADD TEXT TO CACHE
    addItem( text );
    m_head->pos = pos;
    m_head->size = size;
    m_head->flags = flags;
    return m_head->text;
}

/*******************************************************************************/
// XML parser
/*******************************************************************************/


/// states of XML parser
enum parser_state_t {
    ps_bof,
    ps_lt,
    ps_attr,     // waiting for attributes or end of tag
    ps_text,
};


void LVXMLParser::SetCharset( const lChar16 * name )
{
    LVTextFileBase::SetCharset( name );
    m_callback->OnEncoding( name, m_conv_table );
}

void LVXMLParser::Reset()
{
    LVTextFileBase::Reset();
    m_state = ps_bof;
}

LVXMLParser::LVXMLParser( LVStreamRef stream, LVXMLParserCallback * callback )
    : LVTextFileBase(stream)
    , m_callback(callback)
    , m_trimspaces(true)
    , m_state(0)
{
}

LVXMLParser::~LVXMLParser()
{
}

inline bool IsSpaceChar( lChar16 ch )
{
    return (ch == ' ')
        || (ch == '\t')
        || (ch == '\r')
        || (ch == '\n');
}

/// returns true if format is recognized by parser
bool LVXMLParser::CheckFormat()
{
    #define XML_PARSER_DETECT_SIZE 8192
    Reset();
    lChar16 * chbuf = new lChar16[XML_PARSER_DETECT_SIZE];
    FillBuffer( XML_PARSER_DETECT_SIZE );
    int charsDecoded = ReadTextBytes( 0, XML_PARSER_DETECT_SIZE, chbuf+m_buf_pos, m_buf_len-m_buf_pos );
    bool res = false;
    if ( charsDecoded > 100 ) {
        lString16 s( chbuf, charsDecoded );
        if ( s.pos(L"<?xml") >=0 && s.pos(L"<FictionBook") >= 0 )
            res = true;
    }
    delete chbuf;
    Reset();
    return res;
}

bool LVXMLParser::Parse()
{
    //
    Reset();
    bool inXmlTag = false;
    m_callback->OnStart(this);
    bool closeFlag = false;
    bool qFlag = false;
    lString16 tagname;
    lString16 tagns;
    lString16 attrname;
    lString16 attrns;
    lString16 attrvalue;
    for (;!Eof();)
    {
        // load next portion of data if necessary
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
            FillBuffer( MIN_BUF_DATA_SIZE*2 );
        if ( m_buf_len - m_buf_pos <=0 )
            break;
        switch (m_state)
        {
        case ps_bof:
            {
                // skip file beginning until '<'
                for ( ; m_buf_pos<m_buf_len && m_buf[m_buf_pos]!='<'; m_buf_pos++ )
                    ;
                if (m_buf_pos<m_buf_len)
                {
                    // m_buf[m_buf_pos] == '<'
                    m_state = ps_lt;
                    m_buf_pos++;
                }
            }
            break;
        case ps_lt:
            {
                if (!SkipSpaces())
                    break;
                closeFlag = false;
                qFlag = false;
                if (m_buf[m_buf_pos]=='/')
                {
                    m_buf_pos++;
                    closeFlag = true;
                }
                else if (m_buf[m_buf_pos]=='?')
                {
                    // <?xml?>
                    m_buf_pos++;
                    qFlag = true;
                }
                else if (m_buf[m_buf_pos]=='!')
                {
                    // comments etc...
                }
                if (!ReadIdent(tagns, tagname) || m_buf[m_buf_pos]=='=')
                {
                    // error!
                    if (SkipTillChar('>'))
                    {
                        m_state = ps_text;
                        ++m_buf_pos;
                    }
                    break;
                }

                if (closeFlag)
                {
                    m_callback->OnTagClose(tagns.c_str(), tagname.c_str());
                    if (SkipTillChar('>'))
                    {
                        m_state = ps_text;
                        ++m_buf_pos;
                    }
                    break;
                }

                if (qFlag)
                    tagname.insert(0, 1, '?');
                m_callback->OnTagOpen(tagns.c_str(), tagname.c_str());
                inXmlTag = (tagname==L"?xml");

                m_state = ps_attr;
            }
            break;
        case ps_attr:
            {
                if (!SkipSpaces())
                    break;
                char ch = m_buf[m_buf_pos];
                char nch = m_buf[m_buf_pos+1];
                if ( ch=='>' || (nch=='>' && (ch=='/' || ch=='?')) )
                {
                    // end of tag
                    if (ch!='>')
                        m_callback->OnTagClose(tagns.c_str(), tagname.c_str());
                    if (ch=='>')
                        m_buf_pos++;
                    else
                        m_buf_pos+=2;
                    m_state = ps_text;
                    break;
                }
                if ( !ReadIdent(attrns, attrname) )
                {
                    // error: skip rest of tag
                    SkipTillChar('<');
                    m_buf_pos++;
                    m_state = ps_lt;
                    break;
                }
                SkipSpaces();
                attrvalue.reset(16);
                if ( m_buf[m_buf_pos]=='=' )
                {
                    // read attribute value
                    m_buf_pos++;
                    SkipSpaces();
                    lChar16 qChar = 0;
                    lChar16 ch = m_buf[m_buf_pos];
                    if (ch=='\"' || ch=='\'')
                    {
                        qChar = m_buf[m_buf_pos];
                        m_buf_pos++;
                    }
                    for ( ;!Eof(); )
                    {
                        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
                            FillBuffer( MIN_BUF_DATA_SIZE*2 );
                        ch = m_buf[m_buf_pos];
                        if (ch=='>')
                            break;
                        if (!qChar && IsSpaceChar(ch))
                            break;
                        if (qChar && ch==qChar)
                        {
                            m_buf_pos++;
                            break;
                        }
                        ch = ReadChar();
                        if (ch)
                            attrvalue += ch;
                        else
                            break;
                    }
                }
                m_callback->OnAttribute( attrns.c_str(), attrname.c_str(), attrvalue.c_str());
                if (inXmlTag && attrname==L"encoding")
                {
                    SetCharset( attrvalue.c_str() );
                }
            }
            break;
        case ps_text:
            {
                ReadText();
                m_state = ps_lt;
            }
            break;
        default:
            {
            }
        }
    }
    m_callback->OnStop();
    return true;
}

//#define TEXT_SPLIT_SIZE 8192
#define TEXT_SPLIT_SIZE 8192

// returns new length
int PreProcessXmlString( lChar16 * str, int len, lUInt32 flags )
{
    int state = 0;
    lChar16 nch = 0;
    lChar16 lch = 0;
    lChar16 nsp = 0;
    bool pre = (flags & TXTFLG_PRE);
    int j = 0;
    for (int i=0; i<len; ++i )
    {
        lChar16 ch = str[i];
        if ( !pre && (ch=='\r' || ch=='\n' || ch=='\t') )
            ch = ' ';
        if (ch=='\r')
        {
            if ((i==0 || lch!='\n') && (i==len-1 || str[i+1]!='\n'))
                str[j++] = '\n';
        }
        else if (ch=='\n')
        {
            str[j++] = '\n';
        }
        else if (ch=='&')
        {
            state = 1;
            nch = 0;
        }
        else if (state==0)
        {
            if (ch==' ')
            {
                if ( pre || !nsp )
                    str[j++] = ch;
                nsp++;
            }
            else
            {
                str[j++] = ch;
                nsp = 0;
            }
        }
        else
        {
            if (state == 2 && ch>='0' && ch<='9')
                nch = nch * 10 + (ch - '0');
            else if (ch=='#' && state==1)
                state = 2;
            else if (ch == ';')
            {
                if (nch)
                    str[j++] = nch;
                state = 0;
                nsp = 0;
            }
            else
            {
                // error: return to normal mode
                state = 0;
            }
        }
        lch = ch;
    }
    return j;
}

bool LVXMLParser::ReadText()
{
    int text_start_pos = 0;
    int ch_start_pos = 0;
    int last_split_fpos = 0;
    int last_split_txtlen = 0;
    int tlen = 0;
    text_start_pos = (int)(m_buf_fpos + m_buf_pos);
    m_txt_buf.reset(TEXT_SPLIT_SIZE+1);
    for (;!Eof();)
    {
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
            FillBuffer( MIN_BUF_DATA_SIZE*2 );
        ch_start_pos = (int)(m_buf_fpos + m_buf_pos);
        lChar16 ch = ReadChar();
        bool flgBreak = ch=='<' || Eof();
        if (!flgBreak)
        {
            m_txt_buf += ch;
            tlen++;
        }
        if ( tlen > TEXT_SPLIT_SIZE || flgBreak )
        {
            if (last_split_fpos==0 || flgBreak )
            {
                last_split_fpos = (int)((ch=='<')?ch_start_pos : m_buf_fpos + m_buf_pos);
                last_split_txtlen = tlen;
            }
            //=====================================================
            int newlen = PreProcessXmlString( m_txt_buf.modify(), last_split_txtlen, 0 );
            m_callback->OnText(m_txt_buf.c_str(), newlen, text_start_pos, last_split_fpos-text_start_pos, 0 );
            //=====================================================
            if (flgBreak)
            {
                //m_buf_pos++;
                break;
            }
            m_txt_buf.erase(0, last_split_txtlen);
            tlen = m_txt_buf.length();
            text_start_pos = last_split_fpos; //m_buf_fpos + m_buf_pos;
            last_split_fpos = 0;
            last_split_txtlen = 0;
        }
        else if (ch==' ' || (ch=='\r' && m_buf[m_buf_pos]!='\n') 
            || (ch=='\n' && m_buf[m_buf_pos]!='\r') )
        {
            last_split_fpos = (int)(m_buf_fpos + m_buf_pos);
            last_split_txtlen = tlen;
        }
    }
    //if (!Eof())
    //    m_buf_pos++;
    return (!Eof());
}

bool LVXMLParser::SkipSpaces()
{
    while (!Eof())
    {
        for ( ; m_buf_pos<m_buf_len && IsSpaceChar(m_buf[m_buf_pos]); m_buf_pos++ )
            ;
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
            FillBuffer( MIN_BUF_DATA_SIZE*2 );
        if (m_buf_pos<m_buf_len)
            return true; // non-space found!
    }
    return false; // EOF
}

bool LVXMLParser::SkipTillChar( char ch )
{
    while (!Eof())
    {
        for ( ; m_buf_pos<m_buf_len && m_buf[m_buf_pos]!=ch; m_buf_pos++ )
            ;
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
            FillBuffer( MIN_BUF_DATA_SIZE*2 );
        if (m_buf[m_buf_pos]==ch)
            return true; // char found!
    }
    return false; // EOF
}

inline bool isValidIdentChar( char ch )
{
    return ( (ch>='a' && ch<='z')
          || (ch>='A' && ch<='Z')
          || (ch>='0' && ch<='9')
          || (ch=='-')
          || (ch=='_')
          || (ch=='.')
          || (ch==':') );
}

inline bool isValidFirstIdentChar( char ch )
{
    return ( (ch>='a' && ch<='z')
          || (ch>='A' && ch<='Z')
           );
}

// read identifier from stream
bool LVXMLParser::ReadIdent( lString16 & ns, lString16 & name )
{
    // clear string buffer
    ns.reset(16);
    name.reset(16);
    // check first char
    if (! isValidFirstIdentChar(m_buf[m_buf_pos]) )
        return false;
    name += (lChar16)m_buf[m_buf_pos++];
    while (!Eof())
    {
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE )
            FillBuffer( MIN_BUF_DATA_SIZE*2 );
        for ( ; m_buf_pos<m_buf_len; m_buf_pos++ )
        {
            lUInt8 ch = m_buf[m_buf_pos];
            if (!isValidIdentChar(ch))
                break;
            if (ch == ':')
            {
                if ( ns.empty() )
                    name.swap( ns ); // add namespace
                else
                    break; // error
            }
            else
            {
                name += ch;
            }
        }
        if (m_buf_pos<m_buf_len)
        {
            char ch = m_buf[m_buf_pos];
            return (!name.empty()) && (ch==' ' || ch=='/' || ch=='>' || ch=='?' || ch=='=');
        }
    }
    return true; // EOF
}

void LVXMLParser::SetSpaceMode( bool flgTrimSpaces )
{
    m_trimspaces = flgTrimSpaces;
}
