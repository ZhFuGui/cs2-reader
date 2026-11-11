#pragma once
#include <array>
#include <cstdint>

// build14192：与agents_14192.hpp的80个已核验模型逐项对应。
// 每个嵌入mesh均含相同唯一cstrike/19项；这里只保存可选mesh组的并集，不保存实体地址。
// 资源清单SHA256：3c48979333182b81f14277dcd9f0308483538ae25d16eef619cc4a0452a1bcc9
namespace cs2::game::skeleton::profiles {
inline constexpr std::array<std::uint64_t,80> agent_hitbox_group_masks_14192 = {
    0x2fULL, // agents/models/ctm_diver/ctm_diver_varianta.vmdl
    0x2fULL, // agents/models/ctm_diver/ctm_diver_variantb.vmdl
    0x27ULL, // agents/models/ctm_diver/ctm_diver_variantc.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_varianta.vmdl
    0x3eULL, // agents/models/ctm_fbi/ctm_fbi_variantb.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_variantc.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_variantd.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_variante.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_variantf.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_variantg.vmdl
    0x2fULL, // agents/models/ctm_fbi/ctm_fbi_varianth.vmdl
    0x2fULL, // agents/models/ctm_gendarmerie/ctm_gendarmerie_varianta.vmdl
    0x2fULL, // agents/models/ctm_gendarmerie/ctm_gendarmerie_variantb.vmdl
    0x2fULL, // agents/models/ctm_gendarmerie/ctm_gendarmerie_variantc.vmdl
    0x2fULL, // agents/models/ctm_gendarmerie/ctm_gendarmerie_variantd.vmdl
    0x2fULL, // agents/models/ctm_gendarmerie/ctm_gendarmerie_variante.vmdl
    0x3eULL, // agents/models/ctm_sas/ctm_sas.vmdl
    0x3eULL, // agents/models/ctm_sas/ctm_sas_variantf.vmdl
    0x3eULL, // agents/models/ctm_sas/ctm_sas_variantg.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variante.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantg.vmdl
    0x27ULL, // agents/models/ctm_st6/ctm_st6_varianti.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantj.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantk.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantl.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantm.vmdl
    0x2fULL, // agents/models/ctm_st6/ctm_st6_variantn.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_variante.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_variantf.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_variantg.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_varianth.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_varianti.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_variantj.vmdl
    0x3eULL, // agents/models/ctm_swat/ctm_swat_variantk.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_variantf.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_variantg.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_varianth.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_varianti.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_variantj.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_variantk.vmdl
    0xfULL, // agents/models/tm_balkan/tm_balkan_variantl.vmdl
    0xfULL, // agents/models/tm_jungle_raider/tm_jungle_raider_varianta.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantb.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantb2.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantc.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantd.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variante.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantf.vmdl
    0x7ULL, // agents/models/tm_jungle_raider/tm_jungle_raider_variantf2.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_varianta.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantb.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantc.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantd.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variante.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantf.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantg.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_varianth.vmdl
    0x7ULL, // agents/models/tm_leet/tm_leet_varianti.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantj.vmdl
    0xfULL, // agents/models/tm_leet/tm_leet_variantk.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_varianta.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_variantb.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_variantc.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_variantd.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_variantf.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_variantg.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_varianth.vmdl
    0xfULL, // agents/models/tm_phoenix/tm_phoenix_varianti.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf1.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf2.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf3.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf4.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varf5.vmdl
    0x7ULL, // agents/models/tm_professional/tm_professional_varg.vmdl
    0x7ULL, // agents/models/tm_professional/tm_professional_varh.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_vari.vmdl
    0xfULL, // agents/models/tm_professional/tm_professional_varj.vmdl
};
} // namespace cs2::game::skeleton::profiles
