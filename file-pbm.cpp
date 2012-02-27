/*! gimp iff/pbm file reader plugin
\author Adam Hlavatovič
\version 20120227 */
#include <fstream>
using std::ifstream;
#include <stdexcept>
using std::logic_error;
#include <string>
using std::string;
#include <sstream>
using std::ostringstream;
#include <map>
using std::map;
#include <cstring>
#include <libgimp/gimp.h>

#ifdef DEBUG
	#include <iostream>
	using std::cout;
	#include <iomanip>
	using std::hex;
	using std::dec;
#endif


#define LOAD_PROC "file-pbm-load"


struct chunk_info 
{
	uint32_t size;
	uint32_t offset;
};

struct chunk_header 
{
	char magic[4];
	uint32_t size;
};

struct bitmap_header
{
	uint16_t w;
	uint16_t h;
	int16_t x;
	int16_t y;
	uint8_t nplanes;
	uint8_t masking;
	uint8_t compression;
	uint8_t flags;
	uint16_t transparent_color;
	uint8_t x_aspect;
	uint8_t y_aspect;
	int16_t page_width;
	int16_t page_height;
};

struct color_map
{
	uint8_t * colors;
	size_t ncolors;

	color_map() {}
	color_map(uint8_t * c, size_t n) : colors(c), ncolors(n) {}
};

class pbm_image
{
public:
	void load(string const & filename) throw(logic_error);
	size_t w() const {return _header.w;}
	size_t h() const {return _header.h;}
	bool has_colormap() const {return _cmap.colors;}
	color_map const & colormap() const {return _cmap;}
	uint8_t const * data() const {return _body;}

private:
	color_map _cmap;
	uint8_t * _body;
	bitmap_header _header;
};

logic_error cant_open_file(string const & filename)
{
	ostringstream o;
	o << "could not open '" << filename << "' file";
	return logic_error(o.str());
}

logic_error unknown_image_format(uint8_t * buf, int n)
{
	ostringstream o;
	o << "unknown image format (magic='" 
		<< string((char *)buf, (char *)buf+n) << "')";
	return logic_error(o.str());
}

logic_error chunk_size_field_corrupted(char * magic, uint32_t size)
{
	ostringstream o;
	o << "chunk size field corrupted, " << magic << ".size:" << size;
	return logic_error(o.str());	
}

string error_string;

// compressions
#define cmpNone 0
#define cmpByteRun1 1

// masking
#define mskNone 0
#define mskHasMask 1
#define mskHasTransparentColor 2
#define mskLasso 3


chunk_header read_chunk_header(ifstream & fin, uint32_t offset);
gint32 gimp_image(string const & filename);
gint32 create_gimp_image(string const & name, pbm_image const & pbm);
bitmap_header process_bmhd(ifstream & fin, chunk_info const & info);
color_map process_cmap(ifstream & fin, chunk_info const & info,
	bitmap_header const & head);
uint8_t * process_body(ifstream & fin, chunk_info const & info, 
	bitmap_header const & head);
void list_chunks(ifstream & fin, uint32_t form_size, 
	map<string, chunk_info> & chunks);

bool unpack_byte_run_1(uint8_t * first, uint8_t * last, uint8_t * result, 
	uint32_t result_size);

//! Big to little endian conversion.
//@{
void tole2() {}

template <typename T, typename ... Args>
inline void tole2(T & value, Args & ... args)
{
	uint8_t * p = (uint8_t *)&value;
	value = (p[0] << 8)|p[1];
	tole2(args ...);
}

void tole4() {}

template <typename T, typename ... Args>
void tole4(T & value, Args & ... args)
{
	uint8_t * p = (uint8_t *)&value;
	value = (p[0] << 24)|(p[1] << 16)|(p[2] << 8)|p[3];
	tole4(args ...);
}
//@}

void pbm_image::load(string const & filename) throw(logic_error)
{
	ifstream fin(filename);
	if (!fin.is_open())
		throw cant_open_file(filename);

	uint8_t buf[12];
	fin.read((char *)buf, 12);
	
	if (strncmp((char const *)buf, "FORM", 4))
		throw logic_error("not an ea85 iff container");
	
	if (strncmp((char const *)buf+8, "PBM", 3))
		throw unknown_image_format(buf, 3);

	uint32_t form_size;
	memcpy((void *)&form_size, (void *)(buf+4), 4);
	tole4(form_size);

	map<string, chunk_info> chunks;
	list_chunks(fin, form_size, chunks);

	// BMHD
	auto it = chunks.find("BMHD");
	if (it == chunks.end())
		throw logic_error("bitmap header missing");
	_header = process_bmhd(fin, it->second);

	// not all features are now supported
#ifdef DEBUG	
	cout << "planes:" << int(_header.nplanes) << "\n";
	cout << "masking:" << int(_header.masking) << "\n";
#endif

	if (_header.nplanes != 8)
		throw logic_error("only 8bit planes images are supported");

	if (_header.masking != mskNone)
		throw logic_error("masked images are not yet supported");

	// CMAP
	it = chunks.find("CMAP");
	if (it != chunks.end())
		_cmap = process_cmap(fin, it->second, _header);		

	// BODY
	it = chunks.find("BODY");
	if (it != chunks.end())
	{
		_body = process_body(fin, it->second, _header);
		if (!_body)
			throw logic_error("image data corrupded, can't unpack");
	}
}

void list_chunks(ifstream & fin, uint32_t form_size, 
	map<string, chunk_info> & chunks)
{
	uint32_t offset = fin.tellg();
	uint32_t end_offset = offset + form_size - 4;
	
#ifdef DEBUG	
	cout << hex << "offset:" << offset << "\n"
		<< "end_offset:" << end_offset << dec << "\n";
#endif	

	while (fin && (offset < end_offset))
	{
		chunk_info info;
		info.offset = offset + 8;

		chunk_header head = read_chunk_header(fin, offset);

		char magic[5];
		char * t = magic;
		char * s = head.magic;
		while (isalnum(*s) && t != magic+4)
			*t++ = *s++;
		*t = '\0';

		info.size = head.size;
		if (head.size > form_size)
			throw chunk_size_field_corrupted(magic, head.size);
		
		chunks.insert(make_pair(string(magic), info));

		int padding = info.size % 2;
		offset = info.offset + info.size + padding;

#ifdef DEBUG
		cout << "chunk {magic:'" << magic 
			<< hex << "', offset:" << info.offset
			<< dec << ", size:" << info.size << "} append\n";
#endif
	}
}

chunk_header read_chunk_header(ifstream & fin, uint32_t offset)
{
	fin.seekg(offset);
	chunk_header head;
	fin.read((char *)&head, sizeof(chunk_header));
	tole4(head.size);
	return head;
}


void query();
void run(gchar const * name, gint nparams, GimpParam const * param,
	gint * nreturn_vals, GimpParam ** return_vals);


const GimpPlugInInfo PLUG_IN_INFO = {NULL, NULL, query, run};


MAIN()


void query()
{
	static const GimpParamDef load_args[] = {
		{GIMP_PDB_INT32, "run-mode", "Interactive, non-interactive"},
		{GIMP_PDB_STRING, "filename", "The name of the file to load"},
		{GIMP_PDB_STRING, "raw-filename", "The name entered"}
	};

	static const GimpParamDef load_return_vals[] = {
		{GIMP_PDB_IMAGE, "image", "Output image"}
	};

	gimp_install_procedure(
		LOAD_PROC,
		"blurb",
		"help",
		"Adam Hlavatovic",
		"copyleft",
		"2012",
		"menu label",
		NULL,
		GIMP_PLUGIN,
		G_N_ELEMENTS(load_args),
		G_N_ELEMENTS(load_return_vals),
		load_args, load_return_vals);

	gimp_register_file_handler_mime(LOAD_PROC, "image/lbm");
	gimp_register_magic_load_handler(LOAD_PROC, "lbm",	"", "0,string,FORM");
}

void run(gchar const * name, gint nparams, GimpParam const * param,
	gint * nreturn_vals, GimpParam ** return_vals)
{	
	static GimpParam values[2];
	*nreturn_vals = 1;
	*return_vals = values;
	values[0].type = GIMP_PDB_STATUS;
	values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

	if (strcmp(name, LOAD_PROC))
		return;  // unknown procedure

	GimpPDBStatusType status = GIMP_PDB_SUCCESS;

	try {
		gint32 image = gimp_image(param[1].data.d_string);
		if (image == -1)
			throw logic_error("could not create gimp image");

		*nreturn_vals = 2;
		values[1].type = GIMP_PDB_IMAGE;
		values[1].data.d_image = image;

		values[0].data.d_status = status;
	} 
	catch (logic_error const & e) {
		error_string = e.what();
		status = GIMP_PDB_EXECUTION_ERROR;
		*nreturn_vals = 2;
		values[1].type = GIMP_PDB_STRING;
		values[1].data.d_string = const_cast<gchar *>(error_string.c_str());
	};

	values[0].data.d_status = status;
}

gint32 gimp_image(string const & filename)
{
	pbm_image pbm;
	pbm.load(filename);
	return create_gimp_image(filename, pbm);
}

gint32 create_gimp_image(string const & name, pbm_image const & pbm)
{
	// new image and layer
	GimpImageBaseType base_type = GIMP_INDEXED;
	gint32 image = gimp_image_new(pbm.w(), pbm.h(), base_type);

	GimpImageType image_type = GIMP_INDEXED_IMAGE;
	gint32 layer = gimp_layer_new(image, "Background", pbm.w(), pbm.h(), 
		image_type, 100, GIMP_NORMAL_MODE);

	gimp_image_add_layer(image, layer, 0);
	GimpDrawable * drawable = gimp_drawable_get(layer);

	gimp_image_set_filename(image, name.c_str());

	// fill image
	GimpPixelRgn pixel_rgn;
	gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
		drawable->height, TRUE, FALSE);

	gimp_pixel_rgn_set_rect(&pixel_rgn, pbm.data(), pixel_rgn.x, 
		pixel_rgn.y, pixel_rgn.w, pixel_rgn.h);

	// color map
	color_map const & cmap = pbm.colormap();
	gimp_image_set_colormap(image, cmap.colors, cmap.ncolors);

	// apply changes
	gimp_drawable_flush(drawable);
	gimp_drawable_detach(drawable);

	return image;
}

bitmap_header process_bmhd(ifstream & fin, chunk_info const & info)
{
	fin.seekg(info.offset);
	bitmap_header head;
	fin.read((char *)&head, info.size);
	tole2(head.w, head.h, head.x, head.y, head.transparent_color,
		head.page_width, head.page_height);
	return head;
}

color_map process_cmap(ifstream & fin, chunk_info const & info,
	bitmap_header const & head)
{
	// only 3-byte rgb triplets are supported for now
	fin.seekg(info.offset);
	uint8_t * cmap = new uint8_t[info.size];
	fin.read((char *)cmap, info.size);
	return color_map(cmap, info.size/3);
}

uint8_t * process_body(ifstream & fin, chunk_info const & info, 
	bitmap_header const & head)
{
	fin.seekg(info.offset);
	uint8_t * body = new uint8_t[info.size];
	fin.read((char *)body, info.size);

	if (head.compression == cmpNone)
		return body;

	int ch = 1;  // supports only 8bit planes
	uint32_t image_size = head.w * head.h * ch;
	uint8_t * unpacked = new uint8_t[image_size];
	bool is_unpacked = unpack_byte_run_1(body, body + info.size, unpacked,
		image_size);
	
	delete [] body;

	if (is_unpacked)
		return unpacked;
	else
	{
		delete [] unpacked;
		return nullptr;
	}
}

bool unpack_byte_run_1(uint8_t * first, uint8_t * last, uint8_t * result, 
	uint32_t result_size)
{
	while (result_size)
	{
		int8_t n = *first++;

		if (n >= 0)
		{
			uint8_t * end = first+(n+1);
			for (; first != end && result_size; ++first, ++result, --result_size)
				*result = *first;
		}
		else if (n < 0 && n != -128)
		{
			n = -n+1;
			for (; n && result_size; --n, ++result, --result_size)
				*result = *first;
			++first;
		}
	}

	return first == last;
}


