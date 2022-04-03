#include <staking.hpp>

void staking::ontransfer(name from, name to, asset quantity, string memo) {
    if (to != get_self() || from == get_self() || from == TOKEN_ISSUER) {
        return;
    }
    auto code = get_first_receiver();
    auto sym = quantity.symbol;
    if (code == TOKEN_CONTRACT && sym == TOKEN_SYMBOL) {
        _stake(from, quantity, symbol_code(memo));
    } else {
        _unstake(from, quantity, code, sym);
    }
}


void staking::init(uint64_t number, uint64_t length, uint64_t start_time) {
    require_auth(ADMIN_ACCOUNT);
    check(_epoch.number == 0, "Epoch has inited");
    _epoch.number = number;
    _epoch.length = length;
    _epoch.end_time = start_time + length;
    _epochs.set(_epoch, _self);
}

void staking::addsymbol(symbol sym, name sname, uint64_t rate, uint64_t lock_time) {
    require_auth(ADMIN_ACCOUNT);
    auto supply = get_supply(sname, sym.code());
    check(supply.amount == 0, "The staked symbol has supplied");
    _symbols.emplace(_self, [&](auto &s) {
         s.sym = sym;
         s.sname = sname;
         s.rate = rate;
         s.lock_time = lock_time;
         s.distribute = asset(0, TOKEN_SYMBOL);
         s.locked = asset(0, TOKEN_SYMBOL);
         s.issued = asset(0, sym);
    });
}

void staking::removesymbol(symbol_code sc) {
    require_auth(ADMIN_ACCOUNT);
    auto itr = _symbols.require_find(sc.raw(), "Staked symbol not found");
    check(itr->locked.amount == 0 && itr->locked.amount == 0, "Cannot delete non-empty symbol");
    _symbols.erase(itr);
}

void staking::updaterate(symbol_code sc, uint64_t rate) {
    require_auth(ADMIN_ACCOUNT);
    check(rate < 1000000, "Rate too large");
    auto itr = _symbols.require_find(sc.raw(), "Staked symbol not found");
    _symbols.modify(itr, same_payer, [&](auto &s) {
        s.rate = rate;
    });
}

void staking::distribute() {
    auto now_ts = current_time_point().sec_since_epoch();
    if (now_ts > _epoch.end_time) {
        _epoch.number++;
        _epoch.end_time += _epoch.length;
        uint64_t total_distribute = 0;
        uint64_t ift_supply = get_supply(TOKEN_CONTRACT, TOKEN_SYMBOL.code()).amount;
        auto itr = _symbols.begin();
        while (itr != _symbols.end()) {
            if (itr->rate > 0) {
                total_distribute += _distribute(itr, ift_supply);
            }
            itr++;
        }
        _epoch.distribute.amount = total_distribute;
        _epochs.set(_epoch, _self);
    }
    
}


void staking::stake(name owner, asset quantity, symbol_code staked_sc) {
    require_auth(_self);

    check(quantity.amount > 10000000LL, "The stake amount must be greater than 0.1");
    auto itr = _symbols.require_find(staked_sc.raw(), "Staked symbol not found");

    
    uint128_t ratio = 100000000LL;
    if (itr->locked.amount > 0 && itr->issued.amount > 0) {
        ratio = ratio * itr->issued.amount / itr->locked.amount;
    }
    auto new_issue = asset(quantity.amount * ratio / 100000000LL, itr->sym);
    _symbols.modify(itr, same_payer, [&](auto &s) {
        s.locked += quantity;
        s.issued += new_issue;
    });
    
    auto data1 = std::make_tuple(_self, new_issue, string("stake"));
    action(permission_level{_self, "active"_n}, itr->sname, "issue"_n, data1).send();
    auto data2 = std::make_tuple(_self, owner, new_issue, string("stake"));
    action(permission_level{_self, "active"_n}, itr->sname, "transfer"_n, data2).send();
}

void staking::unstake(name owner, asset quantity, name code, symbol sym) {
    
    require_auth(_self);

    auto itr = _symbols.require_find(sym.code().raw(), "Staked symbol not found");
    check(itr->sname == code, "Incorrect symbol contract");

    uint128_t ratio = 100000000LL;
    if (itr->issued.amount > 0) {
        ratio = ratio * itr->locked.amount / itr->issued.amount;
    }
    auto release = asset(quantity.amount * ratio / 100000000LL, TOKEN_SYMBOL);
    _symbols.modify(itr, same_payer, [&](auto &s) {
        s.locked -= release;
        s.issued -= quantity;
    });

    auto data1 = std::make_tuple(quantity, string("unstake retire"));
    action(permission_level{_self, "active"_n}, itr->sname, "retire"_n, data1).send();
    auto data2 = std::make_tuple(_self, owner, release, string("unstake"));
    action(permission_level{_self, "active"_n}, TOKEN_CONTRACT, "transfer"_n, data2).send();
}

uint64_t staking::_distribute(symbols_mi::const_iterator sym_itr, uint64_t ift_supply) {
    uint64_t distribute = uint128_t(ift_supply) * sym_itr->rate / 1000000;
    if (distribute > 0) {
        _symbols.modify(sym_itr, same_payer, [&](auto &s) {
            s.distribute.amount = distribute;
            s.locked.amount += distribute;
        });
        auto data1 = std::make_tuple(TOKEN_ISSUER, asset(distribute, TOKEN_SYMBOL), string("distribute"));
        action(permission_level{TOKEN_ISSUER, "active"_n}, TOKEN_CONTRACT, "issue"_n, data1).send();
        auto data2 = std::make_tuple(TOKEN_ISSUER, _self, asset(distribute, TOKEN_SYMBOL), string("distribute"));
        action(permission_level{TOKEN_ISSUER, "active"_n}, TOKEN_CONTRACT, "transfer"_n, data2).send();
    }
    return distribute;
}


void staking::_stake(name from, asset quantity, symbol_code staked_sc) {
    check(_epoch.number > 0, "Stake not started");
    check(quantity.symbol.code() == symbol_code("IFT"), "Invalid token");
    check(quantity.amount > 0, "Stake need greater than zero");

    auto now_ts = current_time_point().sec_since_epoch();
    if (now_ts > _epoch.end_time) {
        action(permission_level{_self, "active"_n}, _self, "distribute"_n, std::make_tuple()).send();
    }

    auto data = std::make_tuple(from, quantity, staked_sc);
    action(permission_level{_self, "active"_n}, _self, "stake"_n, data).send();
}

void staking::_unstake(name from, asset quantity, name code, symbol sym) {
    check(_epoch.number > 0, "Unstake not started");
    check(quantity.amount > 0, "Unstake need greater than zero");

    auto now_ts = current_time_point().sec_since_epoch();
    if (now_ts > _epoch.end_time) {
        action(permission_level{_self, "active"_n}, _self, "distribute"_n, std::make_tuple()).send();
    }

    auto data = std::make_tuple(from, quantity, code, sym);
    action(permission_level{_self, "active"_n}, _self, "unstake"_n, data).send();
}