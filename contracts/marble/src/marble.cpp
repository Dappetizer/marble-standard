#include "../include/marble.hpp"

//======================== admin actions ========================

ACTION marble::init(string contract_name, string contract_version, name initial_admin) {
    
    //authenticate
    require_auth(get_self());

    //open config table
    config_table configs(get_self(), get_self().value);

    //validate
    check(!configs.exists(), "config already initialized");
    check(is_account(initial_admin), "initial admin account doesn't exist");

    //initialize
    config new_conf = {
        contract_name, //contract_name
        contract_version, //contract_version
        initial_admin, //admin
        uint64_t(0) //last_serial
    };

    //set new config
    configs.set(new_conf, get_self());

}

ACTION marble::setversion(string new_version) {
    
    //get config
    config_table configs(get_self(), get_self().value);
    auto conf = configs.get();

    //authenticate
    require_auth(conf.admin);

    //set new contract version
    conf.contract_version = new_version;

    //update configs table
    configs.set(conf, get_self());

}

ACTION marble::setadmin(name new_admin) {
    
    //open config table, get config
    config_table configs(get_self(), get_self().value);
    auto conf = configs.get();

    //authenticate
    require_auth(conf.admin);

    //validate
    check(is_account(new_admin), "new admin account doesn't exist");

    //set new admin
    conf.admin = new_admin;

    //update config table
    configs.set(conf, get_self());

}

//======================== group actions ========================

ACTION marble::newgroup(string title, string description, name group_name, name manager, uint64_t supply_cap) {
    
    //open config table, get config
    config_table configs(get_self(), get_self().value);
    auto conf = configs.get();

    //authenticate
    require_auth(conf.admin);

    //open groups table, search for group
    groups_table groups(get_self(), get_self().value);
    auto g_itr = groups.find(group_name.value);

    //validate
    check(g_itr == groups.end(), "group name already taken");
    check(supply_cap > 0, "supply cap must be greater than zero");
    check(is_account(manager), "manager account doesn't exist");

    //emplace new group
    //ram payer: self
    groups.emplace(get_self(), [&](auto& col) {
        col.title = title;
        col.description = description;
        col.group_name = group_name;
        col.manager = manager;
        col.supply = 0;
        col.issued_supply = 0;
        col.supply_cap = supply_cap;
    });

    //initialize
    map<name, bool> initial_behaviors;
    initial_behaviors["mint"_n] = true;
    initial_behaviors["transfer"_n] = true;
    initial_behaviors["activate"_n] = false;
    initial_behaviors["consume"_n] = false;
    initial_behaviors["destroy"_n] = true;

    //for each initial behavior, emplace new behavior
    for (pair p : initial_behaviors) {

        //open behaviors table, search for behavior
        behaviors_table behaviors(get_self(), group_name.value);
        auto bhvr_itr = behaviors.find(p.first.value);

        //validate
        check(bhvr_itr == behaviors.end(), "behavior already exists");

        //emplace new behavior
        //ram payer: self
        behaviors.emplace(get_self(), [&](auto& col) {
            col.behavior_name = p.first;
            col.state = p.second;
        });

    }

}

ACTION marble::editgroup(name group_name, string new_title, string new_description) {

    //get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //modify group
    groups.modify(grp, same_payer, [&](auto& col) {
        col.title = new_title;
        col.description = new_description;
    });

}

ACTION marble::setmanager(name group_name, name new_manager, string memo) {
    
    //get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //validate
    check(is_account(new_manager), "new manager account doesn't exist");

    //modify group
    groups.modify(grp, same_payer, [&](auto& col) {
        col.manager = new_manager;
    });

}

//======================== behavior actions ========================

ACTION marble::addbehavior(name group_name, name behavior_name, bool initial_state) {
    
    //get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //search for behavior
    behaviors_table behaviors(get_self(), group_name.value);
    auto bhvr_itr = behaviors.find(behavior_name.value);

    //validate
    check(bhvr_itr == behaviors.end(), "behavior already exists");

    //emplace new behavior
    //ram payer: self
    behaviors.emplace(get_self(), [&](auto& col) {
        col.behavior_name = behavior_name;
        col.state = initial_state;
    });

}

ACTION marble::toggle(name group_name, name behavior_name) {
    
    //get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //get behavior
    behaviors_table behaviors(get_self(), group_name.value);
    auto& bhvr = behaviors.get(behavior_name.value, "behavior not found");

    //modify behavior
    behaviors.modify(bhvr, same_payer, [&](auto& col) {
        col.state = !bhvr.state;
    });

}

ACTION marble::rmvbehavior(name group_name, name behavior_name) {
    
    //get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //get behavior
    behaviors_table behaviors(get_self(), group_name.value);
    auto& bhvr = behaviors.get(behavior_name.value, "behavior not found");

    //erase behavior
    behaviors.erase(bhvr);

}

//======================== item actions ========================

ACTION marble::mintitem(name to, name group_name) {
    
    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group_name.value, "group name not found");

    //authenticate
    require_auth(grp.manager);

    //open behaviors table, get behavior
    behaviors_table behaviors(get_self(), group_name.value);
    auto& bhvr = behaviors.get(name("mint").value, "behavior not found");

    //validate
    check(bhvr.state, "item is not mintable");

    //validate
    check(is_account(to), "to account doesn't exist");
    check(grp.supply < grp.supply_cap, "supply cap reached");

    //open config table, get configs
    config_table configs(get_self(), get_self().value);
    auto conf = configs.get();

    //initialize
    auto now = time_point_sec(current_time_point());
    uint64_t new_serial = conf.last_serial + 1;
    string logevent_memo = string("serial: ", new_serial);

    //increment last_serial, set new config
    conf.last_serial += 1;
    configs.set(conf, get_self());

    //open items table, find item
    items_table items(get_self(), get_self().value);
    auto itm = items.find(new_serial);

    //validate
    check(itm == items.end(), "serial already exists");

    //emplace new item
    //ram payer: self
    items.emplace(get_self(), [&](auto& col) {
        col.serial = new_serial;
        col.group = group_name;
        col.owner = to;
    });

    //update group
    groups.modify(grp, same_payer, [&](auto& col) {
        col.supply += 1;
        col.issued_supply += 1;
    });

    //inline logevent
    action(permission_level{get_self(), name("active")}, get_self(), name("logevent"), make_tuple(
        "newserial"_n, //event_name
        int64_t(new_serial), //event_value
        now, //event_time
        logevent_memo //memo
    )).send();

}

ACTION marble::transferitem(name from, name to, vector<uint64_t> serials, string memo) {

    //validate
    check(is_account(to), "to account doesn't exist");

    //loop over serials
    for (uint64_t s : serials) {

        //open items table, get item
        items_table items(get_self(), get_self().value);
        auto& itm = items.get(s, "item not found");

        //authenticate
        require_auth(itm.owner);

        //open groups table, get group
        groups_table groups(get_self(), get_self().value);
        auto& grp = groups.get(itm.group.value, "group not found");

        //open behaviors table, get behavior
        behaviors_table behaviors(get_self(), itm.group.value);
        auto& bhvr = behaviors.get(name("transfer").value, "behavior not found");

        //validate
        check(bhvr.state, "item is not transferable");

        //update item
        items.modify(itm, same_payer, [&](auto& col) {
            col.owner = to;
        });

    }

    //notify from and to accounts
    require_recipient(from);
    require_recipient(to);

}

ACTION marble::activateitem(uint64_t serial) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //authenticate
    require_auth(itm.owner);

    //open behaviors table, get behavior
    behaviors_table behaviors(get_self(), itm.group.value);
    auto& bhvr = behaviors.get(name("activate").value, "behavior not found");

    //validate
    check(bhvr.state, "item is not activatable");

}

ACTION marble::consumeitem(uint64_t serial) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //authenticate
    require_auth(itm.owner);

    //open behaviors table, get behavior
    behaviors_table behaviors(get_self(), itm.group.value);
    auto& bhvr = behaviors.get(name("consume").value, "behavior not found");

    //validate
    check(bhvr.state, "item is not consumable");

    //erase item
    items.erase(itm);

}

ACTION marble::destroyitem(uint64_t serial, string memo) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open behaviors table, get behavior
    behaviors_table behaviors(get_self(), itm.group.value);
    auto& bhvr = behaviors.get(name("destroy").value, "behavior not found");

    //validate
    check(bhvr.state, "item is not destroyable");
    check(grp.supply > 0, "cannot reduce supply below zero");

    //update group
    groups.modify(grp, same_payer, [&](auto& col) {
        col.supply -= 1;
    });

    //erase item
    items.erase(itm);

}

//======================== tag actions ========================

ACTION marble::newtag(uint64_t serial, name tag_name, string content, optional<string> checksum, optional<string> algorithm) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open tags table, search for tag
    tags_table tags(get_self(), serial);
    auto tg_itr = tags.find(tag_name.value);

    //validate
    check(tag_name != name(0), "tag name cannot be empty");
    check(tg_itr == tags.end(), "tag name already exists on item");

    //initialize
    string chsum = "";
    string algo = "";

    if (checksum) {
        chsum = *checksum;
    }

    if (algorithm) {
        algo = *algorithm;
    }

    //emplace tag
    //ram payer: self
    tags.emplace(get_self(), [&](auto& col) {
        col.tag_name = tag_name;
        col.content = content;
        col.checksum = chsum;
        col.algorithm = algo;
    });

}

ACTION marble::updatetag(uint64_t serial, name tag_name, string new_content, optional<string> new_checksum, optional<string> new_algorithm) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open tags table, get tag
    tags_table tags(get_self(), serial);
    auto& tg = tags.get(tag_name.value, "tag not found on item");

    string new_chsum = "";
    string new_algo = tg.algorithm;

    if (new_checksum) {
        new_chsum = *new_checksum;
    }

    if (new_algorithm) {
        new_algo = *new_algorithm;
    }

    //update tag
    tags.modify(tg, same_payer, [&](auto& col) {
        col.content = new_content;
        col.checksum = new_chsum;
        col.algorithm = new_algo;
    });

}

ACTION marble::rmvtag(uint64_t serial, name tag_name, string memo) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open tags table, get tag
    tags_table tags(get_self(), serial);
    auto& tg = tags.get(tag_name.value, "tag not found on item");

    //erase item
    tags.erase(tg);

}

//======================== attribute actions ========================

ACTION marble::newattribute(uint64_t serial, name attribute_name, int64_t initial_points) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open attributes table, get attribute
    attributes_table attributes(get_self(), serial);
    auto attr_itr = attributes.find(attribute_name.value);

    //validate
    check(attr_itr == attributes.end(), "attribute name already exists for item");

    //emplace new attribute
    //ram payer: self
    attributes.emplace(get_self(), [&](auto& col) {
        col.attribute_name = attribute_name;
        col.points = initial_points;
    });

}

ACTION marble::setpoints(uint64_t serial, name attribute_name, int64_t new_points) {
    
    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //open attributes table, get attribute
    attributes_table attributes(get_self(), serial);
    auto& attr = attributes.get(attribute_name.value, "attribute not found");

    //authenticate
    require_auth(grp.manager);

    //set new attribute points
    attributes.modify(attr, same_payer, [&](auto& col) {
        col.points = new_points;
    });

}

ACTION marble::increasepts(uint64_t serial, name attribute_name, uint64_t points_to_add) {
    
    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //open attributes table, get attribute
    attributes_table attributes(get_self(), serial);
    auto& attr = attributes.get(attribute_name.value, "attribute not found");

    //authenticate
    require_auth(grp.manager);

    //validate
    check(points_to_add > 0, "must add greater than zero points");

    //modify attribute points
    attributes.modify(attr, same_payer, [&](auto& col) {
        col.points += points_to_add;
    });

}

ACTION marble::decreasepts(uint64_t serial, name attribute_name, uint64_t points_to_subtract) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open attributes table, get attribute
    attributes_table attributes(get_self(), serial);
    auto& attr = attributes.get(attribute_name.value, "attribute not found");

    //validate
    check(points_to_subtract > 0, "must subtract greater than zero points");

    //modify attribute points
    attributes.modify(attr, same_payer, [&](auto& col) {
        col.points -= points_to_subtract;
    });

}

ACTION marble::rmvattribute(uint64_t serial, name attribute_name) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open attributes table, get attribute
    attributes_table attributes(get_self(), serial);
    auto attr = attributes.find(attribute_name.value);

    //validate
    check(attr != attributes.end(), "attribute not found");

    //erase attribute
    attributes.erase(attr);

}

//======================== event actions ========================

ACTION marble::logevent(name event_name, int64_t event_value, time_point_sec event_time, string memo) {

    //authenticate
    require_auth(get_self());

}

ACTION marble::newevent(uint64_t serial, name event_name, optional<time_point_sec> custom_event_time) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open events table, search for event
    events_table events(get_self(), serial);
    auto evnt_itr = events.find(event_name.value);

    //validate
    check(evnt_itr == events.end(), "event already exists");

    //initialize
    time_point_sec now = time_point_sec(current_time_point());
    time_point_sec new_event_time = now;

    //if custom_event_time given
    if (custom_event_time) {
        new_event_time = *custom_event_time;
    }

    //emplace new event
    //ram payer: self
    events.emplace(get_self(), [&](auto& col) {
        col.event_name = event_name;
        col.event_time = new_event_time;
    });

}

ACTION marble::seteventtime(uint64_t serial, name event_name, time_point_sec new_event_time) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open events table, get event
    events_table events(get_self(), serial);
    auto& evnt = events.get(event_name.value, "event not found");

    //modify event
    events.modify(evnt, same_payer, [&](auto& col) {
        col.event_time += new_event_time;
    });

}

ACTION marble::rmvevent(uint64_t serial, name event_name) {

    //open items table, get item
    items_table items(get_self(), get_self().value);
    auto& itm = items.get(serial, "item not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(itm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open events table, get event
    events_table events(get_self(), serial);
    auto& evnt = events.get(event_name.value, "event not found");

    //erase event
    events.erase(evnt);

}

//======================== frame actions ========================

ACTION marble::newframe(name frame_name, name group, map<name, string> default_tags, map<name, int64_t> default_attributes) {

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //open frames table, find frame
    frames_table frames(get_self(), get_self().value);
    auto frm_itr = frames.find(frame_name.value);

    //validate
    check(frm_itr == frames.end(), "frame already exists");

    //emplace new frame
    //ram payer: self
    frames.emplace(get_self(), [&](auto& col) {
        col.frame_name = frame_name;
        col.group = group;
        col.default_tags = default_tags;
        col.default_attributes = default_attributes;
    });

}

ACTION marble::applyframe(name frame_name, uint64_t serial, bool overwrite) {

    //open frames table, get frame
    frames_table frames(get_self(), get_self().value);
    auto& frm = frames.get(frame_name.value, "frame not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(frm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //apply default tags
    for (auto itr = frm.default_tags.begin(); itr != frm.default_tags.end(); itr++) {

        //open tags table, find tag
        tags_table tags(get_self(), serial);
        auto tg_itr = tags.find(itr->first.value);

        //NOTE: will skip existing tag with same tag name if overwrite is false

        //if tag not found
        if (tg_itr == tags.end()) {
            
            //emplace new tag
            //ram payer: self
            tags.emplace(get_self(), [&](auto& col) {
                col.tag_name = itr->first;
                col.content = itr->second;
                col.checksum = "";
                col.algorithm = "";
            });

        } else if (overwrite) {

            //overwrite existing tag
            tags.modify(tg_itr, same_payer, [&](auto& col) {
                col.content = itr->second;
                col.checksum = "";
                col.algorithm = "";
            });

        }

    }

    //apply default attributes
    for (auto itr = frm.default_attributes.begin(); itr != frm.default_attributes.end(); itr++) {

        //open attributes table, find attribute
        attributes_table attributes(get_self(), serial);
        auto attr_itr = attributes.find(itr->first.value);

        //NOTE: will skip existing attribute with same attribute name if overwrite is false

        //if attribute not found
        if (attr_itr == attributes.end()) {
            
            //emplace new attribute
            //ram payer: self
            attributes.emplace(get_self(), [&](auto& col) {
                col.attribute_name = itr->first;
                col.points = itr->second;
            });

        } else if (overwrite) {

            //overwrite existing attribute
            attributes.modify(attr_itr, same_payer, [&](auto& col) {
                col.points = itr->second;
            });

        }

    }

}

ACTION marble::rmvframe(name frame_name, string memo) {

    //open frames table, get frame
    frames_table frames(get_self(), get_self().value);
    auto& frm = frames.get(frame_name.value, "frame not found");

    //open groups table, get group
    groups_table groups(get_self(), get_self().value);
    auto& grp = groups.get(frm.group.value, "group not found");

    //authenticate
    require_auth(grp.manager);

    //erase frame
    frames.erase(frm);

}
