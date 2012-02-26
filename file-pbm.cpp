/* Číta súbory iff/pbm pouzité v settlers 2. */
#include <iostream>
using std::cout;
#include <fstream>
using std::ifstream;
#include <cstring>
#include <libgimp/gimp.h>

#define LOAD_PROC "file-pbm-load"


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


void query();
void run(gchar const * name, gint nparams, GimpParam const * param,
	gint * nreturn_vals, GimpParam ** return_vals);

/*! Dekóder byteRun kódovania použitého v IFF.ILBM a IFF.PBM súboroch. */
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
		"author",
		"copyleft",
		"date",
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
	int channels = 1;

	*nreturn_vals = 1;
	*return_vals = values;
	values[0].type = GIMP_PDB_STATUS;
	values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

// read image data
	char const * filename = param[1].data.d_string;

	ifstream fin(filename, ifstream::in);
	if (!fin.is_open())
	{
		cout << "Could not open '" << filename << "' image file!\n";
		return;
	}

	// bhdr
	bitmap_header bhead;
	fin.seekg(0x14);
	fin.read((char *)&bhead, 20);

	tole2(bhead.w, bhead.h, bhead.x, bhead.y, bhead.transparent_color,
		bhead.page_width, bhead.page_height);
	
	// body
	uint32_t body_size = 0;
	fin.seekg(0x10e2);
	fin.read((char *)&body_size, 4);
	tole4(body_size);

	uint8_t * body = new uint8_t[body_size];
	fin.read((char *)body, body_size);

	if (bhead.compression)
	{
		cout << "body_size:" << body_size << ", picture_size:" 
			<< 640*480 << "\n";

		uint32_t bitmap_size = bhead.w * bhead.h * channels;
		uint8_t * unpacked = new uint8_t[bitmap_size];
		if (!unpack_byte_run_1(body, body + body_size, unpacked, 
			bitmap_size))
		{
			cout << "Can't unpack image data.\n";
			return;
		}

		delete [] body;
		body = unpacked;
	}

	// color map
	
	// ak tam je colormap, potom type je INDEXED inak GRAY
	
	uint32_t cmap_size = 0;
	fin.seekg(0x2c);
	fin.read((char *)&cmap_size, 4);
	tole4(cmap_size);

	cout << "cmap size:" << cmap_size << "\n";
	
	uint8_t * cmap = new uint8_t[cmap_size];
	fin.read((char *)cmap, cmap_size);

	fin.close();

// gimp magic

	// new image and layer
	GimpImageBaseType base_type = GIMP_INDEXED;
	gint32 image = gimp_image_new(bhead.w, bhead.h, base_type);

	GimpImageType image_type = GIMP_INDEXED_IMAGE;
	gint32 layer = gimp_layer_new(image, "Background", bhead.w, bhead.h, 
		image_type, 100, GIMP_NORMAL_MODE);

	gimp_image_add_layer(image, layer, 0);
	GimpDrawable * drawable = gimp_drawable_get(layer);

	gimp_image_set_filename(image, name);

	// fill image
	GimpPixelRgn pixel_rgn;
	gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
		drawable->height, TRUE, FALSE);

	gimp_pixel_rgn_set_rect(&pixel_rgn, body, pixel_rgn.x, pixel_rgn.y, 
		pixel_rgn.w, pixel_rgn.h);

	// color map
	gimp_image_set_colormap(image, cmap, 256);

	// apply changes
	gimp_drawable_flush(drawable);
	gimp_drawable_detach(drawable);

	delete [] body;

	*nreturn_vals = 2;
	values[1].type = GIMP_PDB_IMAGE;
	values[1].data.d_image = image;

	GimpPDBStatusType status = GIMP_PDB_SUCCESS;
	values[0].data.d_status = status;
}

guint32 read_image()
{
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


