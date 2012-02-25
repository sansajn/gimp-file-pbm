/* Číta súbory iff/pbm pouzité v settlers 2. */
#include <iostream>
using std::cout;
#include <fstream>
using std::ifstream;
#include <cstring>
#include <libgimp/gimp.h>

#define LOAD_PROC "file-pbm-load"

void query();
void run(gchar const * name, gint nparams, GimpParam const * param,
	gint * nreturn_vals, GimpParam ** return_vals);

const GimpPlugInInfo PLUG_IN_INFO = {NULL, NULL, query, run};

template <typename I32>
inline void b4tole(I32 & val)
{
	val = ((val & 0xFF) << 24)|((val & 0xFF00) << 8)
		|((val & 0xFF0000) >> 8)|((val & 0xFF000000) >> 24);
}

/*! Dekóder byteRun kódovania použitého v IFF.ILBM a IFF.PBM súboroch. */
bool unpack_byte_run_1(uint8_t * first, uint8_t * last, uint8_t * result, 
	uint32_t result_size);


MAIN ()

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
	int w = 640, h = 480, channels = 1;

	*nreturn_vals = 1;
	*return_vals = values;
	values[0].type = GIMP_PDB_STATUS;
	values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

// read image data
	char const * filename = 
		"/home/ja/private/projects/s2rttr/formats/gimp-pbm/test.lbm";

	ifstream fin(filename, ifstream::in);
	if (!fin.is_open())
	{
		cout << "Couldn't open '" << filename << "' image file!\n";
		return;
	}

	// body
	uint32_t body_size = 0;
	fin.seekg(0x10e2);
	fin.read((char *)&body_size, 4);
	b4tole(body_size);

	uint8_t * packed = new uint8_t[body_size];
	fin.read((char *)packed, body_size);

	cout << "body_size:" << body_size << ", picture_size:" << 640*480 << "\n";

	uint8_t * unpacked = new uint8_t[w*h*channels];
	if (!unpack_byte_run_1(packed, packed+body_size, unpacked, 
		w*h*channels))
	{
		cout << "Can't unpack image data.\n";
		return;
	}

	delete [] packed;

	// color map
	uint32_t cmap_size = 0;
	fin.seekg(0x2c);
	fin.read((char *)&cmap_size, 4);
	b4tole(cmap_size);

	cout << "cmap size:" << cmap_size << "\n";
	
	uint8_t * cmap = new uint8_t[cmap_size];
	fin.read((char *)cmap, cmap_size);

	fin.close();

// gimp magic

	// new image and layer
	GimpImageBaseType base_type = GIMP_INDEXED;
	gint32 image = gimp_image_new(w, h, base_type);

	GimpImageType image_type = GIMP_INDEXED_IMAGE;
	gint32 layer = gimp_layer_new(image, "Background", w, h, image_type, 
		100, GIMP_NORMAL_MODE);

	gimp_image_add_layer(image, layer, 0);
	GimpDrawable * drawable = gimp_drawable_get(layer);

	gimp_image_set_filename(image, name);

	// fill image
	GimpPixelRgn pixel_rgn;
	gimp_pixel_rgn_init(&pixel_rgn, drawable, 0, 0, drawable->width,
		drawable->height, TRUE, FALSE);

	gimp_pixel_rgn_set_rect(&pixel_rgn, unpacked, pixel_rgn.x, pixel_rgn.y, 
		pixel_rgn.w, pixel_rgn.h);

	// color map
	gimp_image_set_colormap(image, cmap, 256);

	// apply changes
	gimp_drawable_flush(drawable);
	gimp_drawable_detach(drawable);

	delete [] unpacked;

	*nreturn_vals = 2;
	values[1].type = GIMP_PDB_IMAGE;
	values[1].data.d_image = image;

	GimpPDBStatusType status = GIMP_PDB_SUCCESS;
	values[0].data.d_status = status;
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


