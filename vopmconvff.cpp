//
//  vopmconvff.cpp
//  vopmconvff
//
//  Created by osoumen on 2021/10/24.
//

#include <list>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

#if _WIN32
#define DIRECTORY_SEPARATOR_CHAR '\\'
#else
#define DIRECTORY_SEPARATOR_CHAR '/'
#endif

#define USERPATCH_FORMAT_VERSION	(1)

bool flatten_TL_option = false;

//! エンベロープ、LFOのゲイン調整に使える入力ソース
enum EnvelopeInputSelect {
	kModulatorInput_NoInput = 0,
	kModulatorInput_Max,
	kModulatorInput_NonConstStart,
	kModulatorInput_Velocity = kModulatorInput_NonConstStart,
	kModulatorInput_ChAfterTouch,
	kModulatorInput_KeyScale,
	kModulatorInput_ModWheel,
	kModulatorInput_BreathController,
	kModulatorInput_FootController,
	kModulatorInput_Balance,
	kModulatorInput_General1,
	kModulatorInput_General2,
	kModulatorInput_General3,
	kModulatorInput_General4,
	kModulatorInput_SoundVariation,
	kModulatorInput_Timbre,
	kModulatorInput_TremoloDepth,
	kModulatorInput_InputNum = (kModulatorInput_TremoloDepth + 1) - kModulatorInput_NonConstStart,
};

//! パッチの種別を表す先頭1バイト
enum DriverPatchType {
	kPatchTypeUndefined	= 0x00,
	
	kPatchTypeOPM_FM	= 0x01,
	
	kPatchTypeOPN_FM	= 0x02,
	kPatchTypeOPN_FMch3	= 0x03,
	kPatchTypeOPN_SSG	= 0x04,
	kPatchTypeOPN_RHYTHM= 0x05,
	kPatchTypeOPNA_ADPCM= 0x06,
	
	kPatchTypeOPL3_FM2op= 0x07,
	kPatchTypeOPL3_FM4op= 0x08,
	kPatchTypeOPL3_RHYTHM= 0x09,
	
	kPatchTypeSPC_PCM	= 0x0a,
	kPatchTypeOPLL_FM	= 0x0b,
	kPatchTypeOPLL_RHYTHM= 0x0c,

	kPatchTypeNum,
	
	kPatchTypeProgram = 0x1f
};

typedef union FfopmPatch {
	struct {
		uint8_t		data[32];
	} raw;
	struct {
		uint8_t		dt1_mul[4];
		uint8_t		tl[4];
		uint8_t		ks_ar[4];
		uint8_t		ame_d1r[4];
		uint8_t		dt2_d2r[4];
		uint8_t		d1l_rr[4];
		uint8_t		fl_con;
		uint8_t		patch_name[7];
	} named;
} FfopmPatch;

//! パッチデータの共通部分(20Byte)
typedef struct PatchCommon {
	uint8_t		patch_type;		///< 音源種別
	uint8_t		lk_nxp_format_version;	///< 1.変更不可 1.エクスポート不可 6.フォーマットバージョン(後方互換性用)
	char		name[14];		///< パッチ名
	uint32_t	original_clock;	///< 作成時のクロック周波数
} PatchCommon;

// LFOのパラメータ(4Byte)
typedef struct LFOParam {
	uint8_t midisync_wf_inputselect;		// 1.3.4
	uint8_t	keyonrst_inputdepth;	// 1.7
	uint8_t freq;
	uint8_t	fadein;
} LFOParam;

// ソフトエンベロープのパラメータ(12Byte)
typedef struct EnvelopeParam {
	uint8_t inputselect;
	int8_t	inputdepth;
	uint8_t	attack_time;
	uint8_t	attack_slope;
	
	uint8_t hold_time;
	uint8_t decay_time;
	uint8_t decay_release_slope;
	uint8_t sustain_level;
	
	uint8_t release_time;
	uint8_t release_level;
	int8_t	key_scaling;		// 最大時に1oct上がる毎にnote=60を基準に半分に
	int8_t	velocity_scaling;	// 80を基準として32上がる毎に倍に
} EnvelopeParam;

// キースケールのカーブ定義(4Byte)
typedef struct KeyScaleParam {
	int8_t	min_level;
	uint8_t	slope1_2;
	int8_t	center_key;
	int8_t	max_level;
} KeyScaleParam;

static const int SWLFO_NUM = 2;
static const int SWENV_NUM = 2;

typedef struct TonedSynthCommon {
	int8_t			transpose;
	int8_t			tuning;
	int8_t			panpot;
	int8_t			panpot_ksl_sens;
	
	int8_t			pitch_lfo_sens[SWLFO_NUM];
	int8_t			pitch_env_sens[SWENV_NUM];
//	int8_t			panpot_lfo_sens[SWLFO_NUM];
//	int8_t			panpot_env_sens[SWENV_NUM];

	KeyScaleParam	ksl;
	EnvelopeParam	sw_env[SWENV_NUM];
	LFOParam		sw_lfo[SWLFO_NUM];
	uint8_t			lfo_delay[SWLFO_NUM];
	int8_t			transpose2;		// OPL3で使用
	int8_t			tuning2;		// OPL3で使用
} TonedSynthCommon;

// スロット毎のパッチデータの共通部分
typedef struct SlotPatchCommon {
	uint8_t		velo_sens;
	uint8_t		tl;
	int8_t		lfo_sens[SWLFO_NUM];
	int8_t		env_sens[SWENV_NUM];
	int8_t		ksl_sens;
} SlotPatchCommon;

typedef struct SlotPatch {
	// VolParamsを必ず先頭に配置
	SlotPatchCommon	common;
	
	uint8_t		dt1_mul;
	
	uint8_t		ks_ar;
	uint8_t		ame_d1r;
	uint8_t		dt2_d2r;
	uint8_t		d1l_rr;
} SlotPatch;

typedef struct OPMPatch {
	// PatchCommonを先頭に必ず配置
	PatchCommon		common_param;
	TonedSynthCommon	tone_param;
	// その後にSlotPatchを配置
	SlotPatch		slot[4];
	
	uint8_t			fl_con;
	uint8_t			slot_mask;
	uint8_t			ne_nfrq;		///< ne=trueの時はvoice7で発音する
	uint8_t			fastrelease[4];
	uint8_t			reserved[5];
	
	void	loadFromFfopm(const FfopmPatch *p);
	void	writeToFfopm(FfopmPatch *p);
} Patch;

const bool is_carrier_table[8][4]={
	{0,0,0,1},
	{0,0,0,1},
	{0,0,0,1},
	{0,0,0,1},
	{0,0,1,1},
	{0,1,1,1},
	{0,1,1,1},
	{1,1,1,1}
};

void OPMPatch::loadFromFfopm(const FfopmPatch *p)
{
	common_param.patch_type = kPatchTypeOPM_FM;
	common_param.lk_nxp_format_version = USERPATCH_FORMAT_VERSION;

	for (int i=0; i<7; ++i) {
		common_param.name[i] = p->named.patch_name[i];
	}
	common_param.name[7] = 0;

	// X68000を想定して4MHzに設定しておく
	common_param.original_clock = 4000000;
	
	for (int op=0; op<4; ++op) {
		slot[op].dt1_mul = p->named.dt1_mul[op];
		slot[op].common.tl = p->named.tl[op];
		slot[op].ks_ar = p->named.ks_ar[op];
		slot[op].ame_d1r = p->named.ame_d1r[op];
		slot[op].dt2_d2r = p->named.dt2_d2r[op];
		slot[op].d1l_rr = p->named.d1l_rr[op];
		slot[op].common.velo_sens = is_carrier_table[p->named.fl_con & 0x07][op]? 127: 0;
	}
	fl_con = p->named.fl_con;
	slot_mask = 15;
	
	// 未使用パラメータは初期化しておく
	tone_param.transpose = 0;
	tone_param.tuning = 0;
	tone_param.panpot = 0;
	tone_param.panpot_ksl_sens = 0;
	tone_param.pitch_lfo_sens[0] = 0;
	tone_param.pitch_lfo_sens[1] = 0;
	tone_param.pitch_env_sens[0] = 0;
	tone_param.pitch_env_sens[1] = 0;
	tone_param.ksl.min_level = 0;
	tone_param.ksl.max_level = 0;
	tone_param.sw_env[0].inputselect = kModulatorInput_NoInput;
	tone_param.sw_env[1].inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[0].midisync_wf_inputselect = kModulatorInput_NoInput;
	tone_param.sw_lfo[1].midisync_wf_inputselect = kModulatorInput_NoInput;
}

void OPMPatch::writeToFfopm(FfopmPatch *p)
{
	for (int op=0; op<4; ++op) {
		p->named.dt1_mul[op] = slot[op].dt1_mul;
		p->named.tl[op] = slot[op].common.tl;
		p->named.ks_ar[op] = slot[op].ks_ar;
		p->named.ame_d1r[op] = slot[op].ame_d1r;
		p->named.dt2_d2r[op] = slot[op].dt2_d2r;
		p->named.d1l_rr[op] = slot[op].d1l_rr;
	}
	p->named.fl_con = fl_con & 0x3f;
	if (tone_param.panpot >= -32) {
		p->named.fl_con |= 0x80;
	}
	if (tone_param.panpot < 32) {
		p->named.fl_con |= 0x40;
	}
	
	for (int i=0; i<7; ++i) {
		p->named.patch_name[i] = common_param.name[i];
	}
}

int	loadFromOPM(const std::string &in_file_path, OPMPatch *patch, int maxPatches)
{
	int numPatches = 0;
	std::ifstream ifs(in_file_path, std::ios::in);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	int patchIndex = 0;
	OPMPatch *p = NULL;
	
	std::string line;
	while (std::getline(ifs, line)) {
		size_t token_pos;
		
		token_pos = line.find("//");
		if (token_pos != std::string::npos) {
			line.erase(token_pos);
		}
		
		token_pos = line.find("@:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 2);
			std::stringstream ss;
			ss << line;
			
			ss >> patchIndex;
			if (patchIndex < maxPatches && patchIndex >= 0) {
				p = &patch[patchIndex];
				
				p->common_param.patch_type = kPatchTypeOPM_FM;
				p->common_param.lk_nxp_format_version = USERPATCH_FORMAT_VERSION;
				// X68000を想定して4MHzに設定しておく
				p->common_param.original_clock = 4000000;
				
				// 未使用パラメータは初期化しておく
				p->tone_param.transpose = 0;
				p->tone_param.tuning = 0;
				p->tone_param.panpot = 0;
				p->tone_param.panpot_ksl_sens = 0;
				p->tone_param.pitch_lfo_sens[0] = 0;
				p->tone_param.pitch_lfo_sens[1] = 0;
				p->tone_param.pitch_env_sens[0] = 0;
				p->tone_param.pitch_env_sens[1] = 0;
				p->tone_param.ksl.min_level = 0;
				p->tone_param.ksl.max_level = 0;
				p->tone_param.sw_env[0].inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_env[1].inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_lfo[0].midisync_wf_inputselect = kModulatorInput_NoInput;
				p->tone_param.sw_lfo[1].midisync_wf_inputselect = kModulatorInput_NoInput;
				
				ss >> std::setw(14) >> p->common_param.name;
				++numPatches;
			}
		}
		
		token_pos = line.find("LFO:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 4);
			std::stringstream ss;
			ss << line;
			
			if (p != NULL) {
				int param;
				ss >> param;
				ss >> param;
				ss >> param;
				ss >> param;
				ss >> param;
				p->ne_nfrq = param & 0x7f;
			}
		}
		
		token_pos = line.find("CH:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			std::stringstream ss;
			ss << line;
			
			if (p != NULL) {
				int param;
				ss >> param;
				p->tone_param.panpot = param - 64;
				ss >> param;
				p->fl_con = (param & 0x07) << 3;
				ss >> param;
				p->fl_con |= param & 0x07;
				ss >> param;
				ss >> param;
				ss >> param;
				p->slot_mask = param >> 3;
				ss >> param;
				p->ne_nfrq = param & 0x80;
			}
		}
		
		int slot = -1;
		token_pos = line.find("M1:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 0;
		}
		token_pos = line.find("M2:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 1;
		}
		token_pos = line.find("C1:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 2;
		}
		token_pos = line.find("C2:");
		if (token_pos != std::string::npos) {
			line.erase(0, token_pos + 3);
			slot = 3;
		}
		
		if (slot != -1) {
			std::stringstream ss;
			ss << line;
			
			SlotPatch *sl = &p->slot[slot];
			if (p != NULL) {
				int param;
				ss >> param;
				sl->ks_ar = param & 0x1f;
				ss >> param;
				sl->ame_d1r = param & 0x1f;
				ss >> param;
				sl->dt2_d2r = param & 0x1f;
				ss >> param;
				sl->d1l_rr = param & 0x0f;
				ss >> param;
				sl->d1l_rr |= (param & 0x0f) << 4;
				ss >> param;
				sl->common.tl = param & 0x7f;
				ss >> param;
				sl->ks_ar |= (param & 0x03) << 6;
				ss >> param;
				sl->dt1_mul = param & 0x0f;
				ss >> param;
				sl->dt1_mul |= (param & 0x07) << 4;
				ss >> param;
				sl->dt2_d2r |= (param & 0x03) << 6;
				ss >> param;
				sl->ame_d1r |= param & 0x80;
			}
		}
	}
	
	return numPatches;
}

void getFilePathExtRemoved(const std::string &path, std::string &out, std::string &outExt);
void processInputFile(const std::string &in_file_path);
int processFFOPMFile(const std::string &in_file_path, const std::string &out_file_path);
int processOPMFile(const std::string &in_file_path, const std::string &out_file_path);
int exportToOPM(const std::string &out_file_path, const OPMPatch *patch, int patchNum);

void getFilePathExtRemoved(const std::string &path, std::string &out, std::string &outExt)
{
	// 拡張子、パス除去処理
	size_t	len = path.length();
	size_t	extPos = len;
	size_t	bcPos = 0;
	
	extPos = path.find_last_of('.');
	
	bcPos = path.find_last_of(DIRECTORY_SEPARATOR_CHAR) + 1;
	
	if (bcPos > extPos) {
		extPos = bcPos;
	}
	
	out = path.substr(0, extPos);
	outExt = path.substr(extPos + 1);
}

void processInputFile(const std::string &in_file_path)
{
	std::string in_file_name;
	std::string in_file_ext;
	getFilePathExtRemoved(in_file_path, in_file_name, in_file_ext);
	
	std::transform(in_file_ext.begin(), in_file_ext.end(), in_file_ext.begin(), ::tolower);
	
	if ((in_file_ext.compare("ffopm") == 0) || in_file_ext.compare("ff") == 0) {
		std::string out_file_path(in_file_name);
		out_file_path.append(".opm");
		processFFOPMFile(in_file_path, out_file_path);
	}
	if (in_file_ext.compare("opm") == 0) {
		std::string out_file_path(in_file_name);
		out_file_path.append(".ffopm");
		processOPMFile(in_file_path, out_file_path);
	}
}

int processFFOPMFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to opm format." << std::endl;

	int numProcessed = 0;
	std::ifstream ifs(in_file_path, std::ios::in | std::ios::binary);
	
	if (ifs.bad()) {
		std::cout << "can not open file: " << in_file_path << std::endl;
		exit(1);
	}
	
	ifs.seekg(0, std::ios::end);
	int fileSize = static_cast<int>(ifs.tellg());
	ifs.seekg(0, std::ios::beg);
	unsigned char *ffopmData = new unsigned char[fileSize];
	ifs.read((char *)ffopmData, fileSize);
	
	
	int	readPtr = 0;
	
	OPMPatch	patch[256];
	int			patchNum = 0;
	
	while (((fileSize - readPtr) >= sizeof(FfopmPatch)) && (patchNum < 256)) {
		FfopmPatch	ffopm;
		::memcpy(ffopm.raw.data, &ffopmData[readPtr], sizeof(FfopmPatch));
		patch[patchNum].loadFromFfopm(&ffopm);
		std::cout << patchNum << ": " << patch[patchNum].common_param.name << std::endl;
		readPtr += sizeof(FfopmPatch);
		++patchNum;
	}
	
	numProcessed = exportToOPM(out_file_path, patch, patchNum);
	
	std::cout << "=>" << out_file_path << std::endl;
	std::cout << "done." << std::endl;

	return numProcessed;
}

int exportToOPM(const std::string &out_file_path, const OPMPatch *patch, int patchNum)
{
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::trunc);
	
	if (!ofs.bad()) {
		ofs << "//MiOPMdrv sound bank Paramer converted by vopmconvff" << std::endl;
		ofs << "//LFO: LFRQ AMD PMD WF NFRQ" << std::endl;
		ofs << "//@:[Num] [Name]" << std::endl;
		ofs << "//CH: PAN	FL CON AMS PMS SLOT NE" << std::endl;
		ofs << "//[OPname]: AR D1R D2R	RR D1L	TL	KS MUL DT1 DT2 AMS-EN" << std::endl;
		
		for (int i=0; i<patchNum; ++i) {
			const OPMPatch *p = &patch[i];
			
			ofs << std::endl;
			ofs << "@:" << i <<  " " << p->common_param.name << std::endl;
			ofs << "LFO:  0   0   0   0"<< std::setw(3) << (p->ne_nfrq & 0x7f) << std::endl;
			ofs << "CH:" << std::setw(3) << (p->tone_param.panpot + 64) << " ";
			ofs << std::setw(3) << ((p->fl_con >> 3) & 0x07) << " ";
			ofs << std::setw(3) << ((p->fl_con) & 0x07) << " ";
			ofs << std::setw(3) << 0 << " ";	// AMS
			ofs << std::setw(3) << 0 << " ";	// PMS
			ofs << std::setw(3) << (p->slot_mask << 3) << " ";
			ofs << std::setw(3) << (p->ne_nfrq & 0x80) << std::endl;
			const int opslot[4] = {0, 2, 1, 3};
			
			const char opName[4][4] = {
				"M1:",
				"C1:",
				"M2:",
				"C2:"
			};
			for (int op=0; op<4; ++op) {
				bool is_carrier = is_carrier_table[((p->fl_con) & 0x07)][opslot[op]];
				const SlotPatch *sl = &p->slot[opslot[op]];
				ofs << opName[op];
				ofs << std::setw(3) << (sl->ks_ar & 0x1f) << " ";
				ofs << std::setw(3) << (sl->ame_d1r & 0x1f) << " ";
				ofs << std::setw(3) << (sl->dt2_d2r & 0x1f) << " ";
				ofs << std::setw(3) << (sl->d1l_rr & 0x0f) << " ";
				ofs << std::setw(3) << (sl->d1l_rr >> 4) << " ";
				if (is_carrier && flatten_TL_option) {
					ofs << "  0 ";
				}
				else {
					ofs << std::setw(3) << (sl->common.tl & 0x7f) << " ";
				}
				ofs << std::setw(3) << (sl->ks_ar >> 6) << " ";
				ofs << std::setw(3) << (sl->dt1_mul & 0x0f) << " ";
				ofs << std::setw(3) << ((sl->dt1_mul >> 4) & 0x07) << " ";
				ofs << std::setw(3) << (sl->dt2_d2r >> 6) << " ";
				ofs << std::setw(3) << (sl->ame_d1r & 0x80) << std::endl;
			}
		}
	}
	else {
		std::cout << "can not open file: " << out_file_path << std::endl;
		return 0;
	}
	
	return patchNum;
}

int processOPMFile(const std::string &in_file_path, const std::string &out_file_path)
{
	std::cout << in_file_path << std::endl;
	std::cout << "Convert to ffopm format." << std::endl;

	OPMPatch	patch[256];
	int numPatches = loadFromOPM(in_file_path, patch, 256);
	
	std::ofstream ofs(out_file_path, std::ios::out | std::ios::binary | std::ios::trunc);

	for (int i=0; i<numPatches; ++i) {
		FfopmPatch p;
		patch[i].writeToFfopm(&p);
		std::cout << i << ": " << patch[i].common_param.name << std::endl;
		ofs.write(reinterpret_cast<const char*>(p.raw.data), sizeof(FfopmPatch));
	}
	
	std::cout << "=>" << out_file_path << std::endl;
	std::cout << "done." << std::endl;
	
	return 0;
}

int main(int argc, const char * argv[]) {
	bool help_mode = false;

	std::list<std::string> in_file_list;
	
	// コマンドラインオプションを処理する
	for (int i=1; i<argc; ++i) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
				case 'f':
					// ff=>opm変換時にキャリアのTLを強制的に0に固定する
					flatten_TL_option = true;
					break;
					
				default:
					std::cout << "Invalid option: " << argv[i] << std::endl;
					help_mode = true;
					break;
			}
		}
		else {
			std::string path(argv[i]);
			in_file_list.push_back(path);
		}
	}
	
	if (help_mode || (in_file_list.size() == 0)) {
		std::cout << "Usage: vopmconvff <input> .." << std::endl;
		std::cout << "<input> supports .ffopm(PMD), .opm(VOPM)" << std::endl;
		std::cout << "Options:" << std::endl;
		std::cout << "  -f         Force carrier TL to 0 when converting to .opm" << std::endl;
		exit(0);
	}
	
	std::for_each(in_file_list.begin(), in_file_list.end(), ::processInputFile);
	
	return 0;
}
