#include <oxenc/endian.h>
#include <oxenc/hex.h>
#include <session/config/groups/info.h>
#include <session/config/groups/keys.h>
#include <session/config/groups/members.h>
#include <sodium/crypto_sign_ed25519.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <iostream>
#include <iterator>
#include <session/config/groups/info.hpp>
#include <session/config/groups/keys.hpp>
#include <session/config/groups/members.hpp>
#include <string_view>

#include "utils.hpp"

using namespace std::literals;
using namespace oxenc::literals;

static constexpr int64_t created_ts = 1680064059;

using namespace session::config;

static std::array<unsigned char, 64> sk_from_seed(ustring_view seed) {
    std::array<unsigned char, 32> ignore;
    std::array<unsigned char, 64> sk;
    crypto_sign_ed25519_seed_keypair(ignore.data(), sk.data(), seed.data());
    return sk;
}

static std::string session_id_from_ed(ustring_view ed_pk) {
    std::string sid;
    std::array<unsigned char, 32> xpk;
    int rc = crypto_sign_ed25519_pk_to_curve25519(xpk.data(), ed_pk.data());
    REQUIRE(rc == 0);
    sid.reserve(66);
    sid += "05";
    oxenc::to_hex(xpk.begin(), xpk.end(), std::back_inserter(sid));
    return sid;
}

// Hacky little class that implements `[n]` on a std::list.  This is inefficient (since it access
// has to iterate n times through the list) but we only use it on small lists in this test code so
// convenience wins over efficiency.  (Why not just use a vector?  Because vectors requires `T` to
// be moveable, so we'd either have to use std::unique_ptr for members, which is also annoying).
template <typename T>
struct hacky_list : std::list<T> {
    T& operator[](size_t n) { return *std::next(std::begin(*this), n); }
};

TEST_CASE("Group Keys - C++ API", "[config][groups][keys][cpp]") {

    struct pseudo_client {
        std::array<unsigned char, 64> secret_key;
        const ustring_view public_key{secret_key.data() + 32, 32};
        std::string session_id{session_id_from_ed(public_key)};

        groups::Info info;
        groups::Members members;
        groups::Keys keys;

        pseudo_client(
                ustring_view seed,
                bool a,
                const unsigned char* gpk,
                std::optional<const unsigned char*> gsk) :
                secret_key{sk_from_seed(seed)},
                info{ustring_view{gpk, 32},
                     a ? std::make_optional<ustring_view>({*gsk, 64}) : std::nullopt,
                     std::nullopt},
                members{ustring_view{gpk, 32},
                        a ? std::make_optional<ustring_view>({*gsk, 64}) : std::nullopt,
                        std::nullopt},
                keys{to_usv(secret_key),
                     ustring_view{gpk, 32},
                     a ? std::make_optional<ustring_view>({*gsk, 64}) : std::nullopt,
                     std::nullopt,
                     info,
                     members} {}
    };

    const ustring group_seed =
            "0123456789abcdeffedcba98765432100123456789abcdeffedcba9876543210"_hexbytes;
    const ustring admin1_seed =
            "0123456789abcdef0123456789abcdeffedcba9876543210fedcba9876543210"_hexbytes;
    const ustring admin2_seed =
            "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"_hexbytes;
    const std::array member_seeds = {
            "000111222333444555666777888999aaabbbcccdddeeefff0123456789abcdef"_hexbytes,  // member1
            "00011122435111155566677788811263446552465222efff0123456789abcdef"_hexbytes,  // member2
            "00011129824754185548239498168169316979583253efff0123456789abcdef"_hexbytes,  // member3
            "0000111122223333444455556666777788889999aaaabbbbccccddddeeeeffff"_hexbytes,  // member4
            "3333333333333333333333333333333333333333333333333333333333333333"_hexbytes,  // member3b
            "4444444444444444444444444444444444444444444444444444444444444444"_hexbytes,  // member4b
    };

    std::array<unsigned char, 32> group_pk;
    std::array<unsigned char, 64> group_sk;

    crypto_sign_ed25519_seed_keypair(
            group_pk.data(),
            group_sk.data(),
            reinterpret_cast<const unsigned char*>(group_seed.data()));
    REQUIRE(oxenc::to_hex(group_seed.begin(), group_seed.end()) ==
            oxenc::to_hex(group_sk.begin(), group_sk.begin() + 32));

    // Using list instead of vector so that `psuedo_client` doesn't have to be moveable, which lets
    // us put the Info/Member/Keys directly inside it (rather than having to use a unique_ptr, which
    // would also be annoying).
    hacky_list<pseudo_client> admins;
    hacky_list<pseudo_client> members;

    // Initialize admin and member objects
    admins.emplace_back(admin1_seed, true, group_pk.data(), group_sk.data());
    admins.emplace_back(admin2_seed, true, group_pk.data(), group_sk.data());

    for (int i = 0; i < 4; ++i)
        members.emplace_back(member_seeds[i], false, group_pk.data(), std::nullopt);

    REQUIRE(admins[0].session_id ==
            "05f1e8b64bbf761edf8f7b47e3a1f369985644cce0a62adb8e21604474bdd49627");
    REQUIRE(admins[1].session_id ==
            "05c5ba413c336f2fe1fb9a2c525f8a86a412a1db128a7841b4e0e217fa9eb7fd5e");
    REQUIRE(members[0].session_id ==
            "05ece06dd8e02fb2f7d9497f956a1996e199953c651f4016a2f79a3b3e38d55628");
    REQUIRE(members[1].session_id ==
            "053ac269b71512776b0bd4a1234aaf93e67b4e9068a2c252f3b93a20acb590ae3c");
    REQUIRE(members[2].session_id ==
            "05a2b03abdda4df8316f9d7aed5d2d1e483e9af269d0b39191b08321b8495bc118");
    REQUIRE(members[3].session_id ==
            "050a41669a06c098f22633aee2eba03764ef6813bd4f770a3a2b9033b868ca470d");

    for (const auto& a : admins)
        REQUIRE(a.members.size() == 0);
    for (const auto& m : members)
        REQUIRE(m.members.size() == 0);

    std::vector<std::pair<std::string, ustring_view>> info_configs;
    std::vector<std::pair<std::string, ustring_view>> mem_configs;

    // add admin account, re-key, distribute
    auto& admin1 = admins[0];

    auto m = admin1.members.get_or_construct(admin1.session_id);
    m.admin = true;
    m.name = "Admin1";
    admin1.members.set(m);

    CHECK(admin1.members.needs_push());

    auto maybe_key_config = admin1.keys.pending_config();
    REQUIRE(maybe_key_config);
    auto new_keys_config1 = *maybe_key_config;

    auto [iseq1, new_info_config1, iobs1] = admin1.info.push();
    admin1.info.confirm_pushed(iseq1, "fakehash1");
    info_configs.emplace_back("fakehash1", new_info_config1);

    auto [mseq1, new_mem_config1, mobs1] = admin1.members.push();
    admin1.members.confirm_pushed(mseq1, "fakehash1");
    mem_configs.emplace_back("fakehash1", new_mem_config1);

    /*  Even though we have only added one admin, admin2 will still be able to see group info
        like group size and merge all configs. This is because they have loaded the key config
        message, which they can decrypt with the group secret key.
    */
    for (auto& a : admins) {
        a.keys.load_key_message(new_keys_config1, get_timestamp(), a.info, a.members);
        CHECK(a.info.merge(info_configs) == 1);
        CHECK(a.members.merge(mem_configs) == 1);
        CHECK(a.members.size() == 1);
    }

    /*  All attempts to merge non-admin members will throw, as none of the non admin members
        will be able to decrypt the new info/member configs using the updated keys
    */
    for (auto& m : members) {
        m.keys.load_key_message(new_keys_config1, get_timestamp(), m.info, m.members);
        CHECK_THROWS(m.info.merge(info_configs));
        CHECK_THROWS(m.members.merge(mem_configs));
        CHECK(m.members.size() == 0);
    }

    info_configs.clear();
    mem_configs.clear();

    // add non-admin members, re-key, distribute
    for (int i = 0; i < members.size(); ++i) {
        auto m = admin1.members.get_or_construct(members[i].session_id);
        m.admin = false;
        m.name = "Member" + std::to_string(i);
        admin1.members.set(m);
    }

    CHECK(admin1.members.needs_push());

    auto new_keys_config2 = admin1.keys.rekey(admin1.info, admin1.members);
    CHECK(not new_keys_config2.empty());

    auto [iseq2, new_info_config2, iobs2] = admin1.info.push();
    admin1.info.confirm_pushed(iseq2, "fakehash2");
    info_configs.emplace_back("fakehash2", new_info_config2);

    auto [mseq2, new_mem_config2, mobs2] = admin1.members.push();
    admin1.members.confirm_pushed(mseq2, "fakehash2");
    mem_configs.emplace_back("fakehash2", new_mem_config2);

    for (auto& a : admins) {
        a.keys.load_key_message(new_keys_config2, get_timestamp(), a.info, a.members);
        CHECK(a.info.merge(info_configs) == 1);
        CHECK(a.members.merge(mem_configs) == 1);
        CHECK(a.members.size() == 5);
    }

    for (auto& m : members) {
        m.keys.load_key_message(new_keys_config2, get_timestamp(), m.info, m.members);
        CHECK(m.info.merge(info_configs) == 1);
        CHECK(m.members.merge(mem_configs) == 1);
        CHECK(m.members.size() == 5);
    }

    info_configs.clear();
    mem_configs.clear();

    // change group info, re-key, distribute
    admin1.info.set_name("tomatosauce"s);

    CHECK(admin1.info.needs_push());

    auto new_keys_config3 = admin1.keys.rekey(admin1.info, admin1.members);
    CHECK(not new_keys_config3.empty());

    auto [iseq3, new_info_config3, iobs3] = admin1.info.push();
    admin1.info.confirm_pushed(iseq3, "fakehash3");
    info_configs.emplace_back("fakehash3", new_info_config3);

    auto [mseq3, new_mem_config3, mobs3] = admin1.members.push();
    admin1.members.confirm_pushed(mseq3, "fakehash3");
    mem_configs.emplace_back("fakehash3", new_mem_config3);

    for (auto& a : admins) {
        a.keys.load_key_message(new_keys_config3, get_timestamp(), a.info, a.members);
        CHECK(a.info.merge(info_configs) == 1);
        CHECK(a.members.merge(mem_configs) == 1);
        CHECK(a.info.get_name() == "tomatosauce"s);
    }

    for (auto& m : members) {
        m.keys.load_key_message(new_keys_config3, get_timestamp(), m.info, m.members);
        CHECK(m.info.merge(info_configs) == 1);
        CHECK(m.members.merge(mem_configs) == 1);
        CHECK(m.info.get_name() == "tomatosauce"s);
    }

    info_configs.clear();
    mem_configs.clear();

    // remove members, re-key, distribute
    CHECK(admin1.members.size() == 5);
    CHECK(admin1.members.erase(members[3].session_id));
    CHECK(admin1.members.erase(members[2].session_id));
    CHECK(admin1.members.size() == 3);

    CHECK(admin1.members.needs_push());

    ustring old_key{admin1.keys.group_enc_key()};
    auto new_keys_config4 = admin1.keys.rekey(admin1.info, admin1.members);
    CHECK(not new_keys_config4.empty());

    CHECK(old_key != admin1.keys.group_enc_key());

    auto [iseq4, new_info_config4, iobs4] = admin1.info.push();
    admin1.info.confirm_pushed(iseq4, "fakehash4");
    info_configs.emplace_back("fakehash4", new_info_config4);

    auto [mseq4, new_mem_config4, mobs4] = admin1.members.push();
    admin1.members.confirm_pushed(mseq4, "fakehash4");
    mem_configs.emplace_back("fakehash4", new_mem_config4);

    for (auto& a : admins) {
        CHECK(a.keys.load_key_message(new_keys_config4, get_timestamp(), a.info, a.members));
        CHECK(a.info.merge(info_configs) == 1);
        CHECK(a.members.merge(mem_configs) == 1);
        CHECK(a.members.size() == 3);
    }

    for (int i = 0; i < members.size(); i++) {
        auto& m = members[i];
        bool found_key =
                m.keys.load_key_message(new_keys_config2, get_timestamp(), m.info, m.members);

        if (i < 2) {  // We should still be in the group
            CHECK(found_key);
            CHECK(m.info.merge(info_configs) == 1);
            CHECK(m.members.merge(mem_configs) == 1);
            CHECK(m.members.size() == 3);
        } else {
            CHECK_FALSE(found_key);
            CHECK(m.info.merge(info_configs) == 0);
            CHECK(m.members.merge(mem_configs) == 0);
            CHECK(m.members.size() == 5);
        }
    }

    members.pop_back();
    members.pop_back();

    info_configs.clear();
    mem_configs.clear();

    // middle-out time
    auto msg = "hello to all my friends sitting in the tomato sauce"s;

    for (int i = 0; i < 5; ++i)
        msg += msg;

    auto compressed = admin1.keys.encrypt_message(to_usv(msg));
    auto uncompressed = admin1.keys.encrypt_message(to_usv(msg), false);

    CHECK(compressed.size() < msg.size());
    CHECK(compressed.size() < uncompressed.size());

    // Add two new members and send them supplemental keys
    for (int i = 0; i < 2; ++i) {
        auto& m = members.emplace_back(member_seeds[4 + i], false, group_pk.data(), std::nullopt);

        auto memb = admin1.members.get_or_construct(m.session_id);
        memb.set_invited();
        admin1.members.set(memb);

        CHECK_FALSE(m.keys.admin());
    }

    REQUIRE(members[2].session_id ==
            "054eb4fafee2bd3018a24e310de8106333c2b364eaed029a7f05d7b45ccc77683a");
    REQUIRE(members[3].session_id ==
            "057ce31baa9a04b5cfb83ab7ccdd7b669b911a082d29883d6aad3256294a0a5e0c");

    // We actually send supplemental keys to members 1, as well, by mistake just to make sure it
    // doesn't do or hurt anything to get a supplemental key you already have.
    std::vector<std::string> supp_sids;
    std::transform(
            std::next(members.begin()), members.end(), std::back_inserter(supp_sids), [](auto& m) {
                return m.session_id;
            });
    auto supp = admin1.keys.key_supplement(supp_sids);
    CHECK(admin1.members.needs_push());
    CHECK_FALSE(admin1.info.needs_push());
    auto [mseq5, mpush5, mobs5] = admin1.members.push();
    mem_configs.emplace_back("fakehash5", mpush5);
    admin1.members.confirm_pushed(mseq5, "fakehash5");
    info_configs.emplace_back("fakehash4", new_info_config4);

    for (size_t i = 0; i < members.size(); i++) {
        DYNAMIC_SECTION("supp key load " << i) {
            auto& m = members[i];
            bool found_key = m.keys.load_key_message(supp, get_timestamp(), m.info, m.members);

            if (i < 1) {
                // This supp key wasn't for us
                CHECK_FALSE(found_key);
                CHECK(m.keys.group_keys().size() == 3);
            } else {
                CHECK(found_key);
                // new_keys_config1 never went to the initial members, but did go out in the
                // supplement, which is why we have the extra key here.
                CHECK(m.keys.group_keys().size() == 4);
            }

            CHECK(m.info.merge(info_configs) == 1);
            CHECK(m.members.merge(mem_configs) == 1);
            REQUIRE(m.info.get_name());
            CHECK(*m.info.get_name() == "tomatosauce"sv);
            CHECK(m.members.size() == 5);
        }
    }
}

TEST_CASE("Group Keys - C++ API", "[config][groups][keys][c]") {
    struct pseudo_client {
        const bool is_admin;

        const ustring seed;
        std::string session_id;

        std::array<unsigned char, 32> public_key;
        std::array<unsigned char, 64> secret_key;

        config_group_keys* keys;
        config_object* info;
        config_object* members;

        pseudo_client(ustring s, bool a, unsigned char* gpk, std::optional<unsigned char*> gsk) :
                seed{s}, is_admin{a} {
            crypto_sign_ed25519_seed_keypair(
                    public_key.data(),
                    secret_key.data(),
                    reinterpret_cast<const unsigned char*>(seed.data()));

            REQUIRE(oxenc::to_hex(seed.begin(), seed.end()) ==
                    oxenc::to_hex(secret_key.begin(), secret_key.begin() + 32));

            std::array<unsigned char, 33> sid;
            int rc = crypto_sign_ed25519_pk_to_curve25519(&sid[1], public_key.data());
            REQUIRE(rc == 0);
            session_id += "\x05";
            oxenc::to_hex(sid.begin(), sid.end(), std::back_inserter(session_id));

            int rv = groups_members_init(&members, gpk, is_admin ? *gsk : NULL, NULL, 0, NULL);
            REQUIRE(rv == 0);

            rv = groups_info_init(&info, gpk, is_admin ? *gsk : NULL, NULL, 0, NULL);
            REQUIRE(rv == 0);

            rv = groups_keys_init(
                    &keys,
                    secret_key.data(),
                    gpk,
                    is_admin ? *gsk : NULL,
                    info,
                    members,
                    NULL,
                    0,
                    NULL);
            REQUIRE(rv == 0);
        }

        ~pseudo_client() {
            config_free(info);
            config_free(members);
        }
    };

    const ustring group_seed =
            "0123456789abcdeffedcba98765432100123456789abcdeffedcba9876543210"_hexbytes;
    const ustring admin1_seed =
            "0123456789abcdef0123456789abcdeffedcba9876543210fedcba9876543210"_hexbytes;
    const ustring admin2_seed =
            "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"_hexbytes;
    const std::array member_seeds = {
            "000111222333444555666777888999aaabbbcccdddeeefff0123456789abcdef"_hexbytes,  // member1
            "00011122435111155566677788811263446552465222efff0123456789abcdef"_hexbytes,  // member2
            "00011129824754185548239498168169316979583253efff0123456789abcdef"_hexbytes,  // member3
            "0000111122223333444455556666777788889999aaaabbbbccccddddeeeeffff"_hexbytes   // member4
    };

    std::array<unsigned char, 32> group_pk;
    std::array<unsigned char, 64> group_sk;

    crypto_sign_ed25519_seed_keypair(
            group_pk.data(),
            group_sk.data(),
            reinterpret_cast<const unsigned char*>(group_seed.data()));
    REQUIRE(oxenc::to_hex(group_seed.begin(), group_seed.end()) ==
            oxenc::to_hex(group_sk.begin(), group_sk.begin() + 32));

    std::vector<pseudo_client> admins;
    std::vector<pseudo_client> members;

    // Initialize admin and member objects
    admins.emplace_back(admin1_seed, true, group_pk.data(), group_sk.data());
    // admins.emplace_back(admin2_seed, true, group_pk.data(), group_sk.data());

    // for (int i = 0; i < 4; ++i)
    //     members.emplace_back(member_seeds[i], false, group_pk.data(), std::nullopt);

    // REQUIRE(admins[0].session_id ==
    //         "05f1e8b64bbf761edf8f7b47e3a1f369985644cce0a62adb8e21604474bdd49627");
    // REQUIRE(admins[1].session_id ==
    //         "05c5ba413c336f2fe1fb9a2c525f8a86a412a1db128a7841b4e0e217fa9eb7fd5e");
    // REQUIRE(members[0].session_id ==
    //         "05ece06dd8e02fb2f7d9497f956a1996e199953c651f4016a2f79a3b3e38d55628");
    // REQUIRE(members[1].session_id ==
    //         "053ac269b71512776b0bd4a1234aaf93e67b4e9068a2c252f3b93a20acb590ae3c");
    // REQUIRE(members[2].session_id ==
    //         "05a2b03abdda4df8316f9d7aed5d2d1e483e9af269d0b39191b08321b8495bc118");
    // REQUIRE(members[3].session_id ==
    //         "050a41669a06c098f22633aee2eba03764ef6813bd4f770a3a2b9033b868ca470d");

    // for (const auto& a : admins)
    //     REQUIRE(contacts_size(a.members) == 0);
    // for (const auto& m : members)
    //     REQUIRE(contacts_size(m.members) == 0);
}
