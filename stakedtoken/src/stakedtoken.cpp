#include <stakedtoken.hpp>

void token::create(const name&   issuer, const asset&  maximum_supply) {
    require_auth(get_self());

    auto sym = maximum_supply.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(maximum_supply.is_valid(), "invalid supply");
    check(maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing == statstable.end(), "token with symbol already exists");

    statstable.emplace(get_self(), [&](auto& s) {
        s.supply.symbol = maximum_supply.symbol;
        s.max_supply    = maximum_supply;
        s.issuer        = issuer;
    });
}


void token::issue(const name& to, const asset& quantity, const string& memo) {
    auto sym = quantity.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
    const auto& st = *existing;
    check(to == st.issuer, "tokens can only be issued to issuer account");

    require_auth(st.issuer);
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must issue positive quantity");

    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify(st, same_payer, [&](auto& s) {
        s.supply += quantity;
    });

    add_balance(st.issuer, quantity, st.issuer, false);
}

void token::retire(const asset& quantity, const string& memo) {
    auto sym = quantity.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing != statstable.end(), "token with symbol does not exist");
    const auto& st = *existing;

    require_auth(st.issuer);
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must retire positive quantity");

    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

    statstable.modify(st, same_payer, [&](auto& s) {
        s.supply -= quantity;
   });

    sub_balance(st.issuer, quantity, false);
}

void token::transfer(const name&    from, const name&    to, const asset&   quantity, const string&  memo) {
    check(from != to, "cannot transfer to self");
    require_auth(from);
    check(is_account(to), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable(get_self(), sym.raw());
    const auto& st = statstable.get(sym.raw());

    require_recipient(from);
    require_recipient(to);

    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must transfer positive quantity");
    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    auto payer = has_auth(to) ? to : from;

    sub_balance(from, quantity, from != st.issuer);
    add_balance(to, quantity, payer, to != st.issuer);

}

void token::sub_balance(const name& owner, const asset& value, bool is_check) {
    accounts from_acnts(get_self(), owner.value);

    const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
    check(from.balance.amount >= value.amount, "overdrawn balance");
    check(!is_check || check_lock(owner, from.balance - value), "transfer amount is greater than locked");

    from_acnts.modify(from, owner, [&](auto& a) {
        a.balance -= value;
    });
}

void token::add_balance(const name& owner, const asset& value, const name& ram_payer, bool add_lock) {
    accounts to_acnts(get_self(), owner.value);
    auto to = to_acnts.find(value.symbol.code().raw());
    if (to == to_acnts.end()) {
        to_acnts.emplace(ram_payer, [&](auto& a){
            a.balance = value;
        });
    } else {
        to_acnts.modify(to, same_payer, [&](auto& a) {
            a.balance += value;
        });
    }

    if (!add_lock) {
        return;
    }

    // add lock
    stakingtable::symbols_mi symbols_tb(STAKING_ACCOUNT, STAKING_ACCOUNT.value);
    auto s_itr = symbols_tb.require_find(value.symbol.code().raw(), "Staked symbol not found");
    // STAKING_ACCOUNT
    uint32_t release_time = current_time_point().sec_since_epoch() + s_itr->lock_time;
    uint32_t dayseconds = 86400;
    uint32_t weekseconds = dayseconds * 7; 
    uint32_t monthseconds = dayseconds * 30; 
    if (s_itr->lock_time >= monthseconds * 3) {
        // group by weeks
        release_time = release_time - (release_time % weekseconds);
    } else if (s_itr->lock_time >= monthseconds) {
        // group by days
        release_time = release_time - (release_time % dayseconds);
    } else if (s_itr->lock_time >= dayseconds) {
        // group by hours
        release_time = release_time - (release_time % 3600);
    } else {
        // group by minutes
        release_time = release_time - (release_time % 60);
    }

    locks_mi locks_tb(_self, owner.value);
    auto locks_idx = locks_tb.get_index<"bysym"_n>();
    auto itr = locks_idx.find(value.symbol.code().raw());
    auto now_time = block_timestamp(current_time_point());
    int count = 0;
    uint64_t lock_id = 0;
    while (itr != locks_idx.end() && itr->sym == value.symbol.code()) {
        if (itr->release_time.to_time_point().sec_since_epoch() == release_time) {
            lock_id = itr->lock_id;
            break;
        }
        if (itr->release_time <= now_time) {
            itr = locks_idx.erase(itr);
            continue;
        } 
        // print_f("% % % %\n", itr->lock_id, itr->release_time.to_string(), itr->sym, value.symbol.code());
        check(++count < 20, "too much staked");
        itr++;
    }

    if (lock_id > 0) {
        auto mitr = locks_tb.find(lock_id);
        locks_tb.modify(mitr, same_payer, [&](auto& a){
            a.amount += value.amount;
        });
    } else {
        locks_tb.emplace(ram_payer, [&](auto& a){
            a.lock_id = locks_tb.available_primary_key();
            a.release_time = time_point(seconds(release_time));
            a.sym = value.symbol.code();
            a.amount = value.amount;
        });
    }

}

bool token::check_lock(const name& owner, const asset& balance) {
    auto balance_amount = balance.amount;
    locks_mi locks_tb(_self, owner.value);
    auto locks_idx = locks_tb.get_index<"bysym"_n>();
    auto itr = locks_idx.find(balance.symbol.code().raw());
    auto now_time = block_timestamp(current_time_point());
    while (itr != locks_idx.end() && itr->sym == balance.symbol.code()) {
        if (itr->release_time <= now_time) {
            itr = locks_idx.erase(itr);
            continue;
        }
        balance_amount -= itr->amount;
        if (balance_amount < 0) {
            return false;
        }
        itr++;
    }
    return true;
}

void token::open(const name& owner, const symbol& symbol, const name& ram_payer) {
    require_auth(ram_payer);

    check(is_account(owner ), "owner account does not exist");

    auto sym_code_raw = symbol.code().raw();
    stats statstable(get_self(), sym_code_raw);
    const auto& st = statstable.get(sym_code_raw, "symbol does not exist");
    check(st.supply.symbol == symbol, "symbol precision mismatch");

    accounts acnts(get_self(), owner.value);
    auto it = acnts.find(sym_code_raw);
    if (it == acnts.end()) {
        acnts.emplace(ram_payer, [&](auto& a){
            a.balance = asset{0, symbol};
        });
    }
}

void token::close(const name& owner, const symbol& symbol) {
    require_auth(owner);
    accounts acnts(get_self(), owner.value);
    auto it = acnts.find(symbol.code().raw());
    check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
    check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
    acnts.erase(it);
}