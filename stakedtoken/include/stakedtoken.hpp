#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include <string>

using namespace eosio;
using std::string;

namespace stakingtable {
    struct st_symbol {
        symbol sym;
        name sname;
        uint64_t rate;
        uint64_t lock_time;
        asset distribute;
        asset locked;
        asset issued;
        uint64_t primary_key() const { return sym.code().raw(); }
    };
    typedef multi_index<"symbols"_n, st_symbol> symbols_mi;

}

class [[eosio::contract("stakedtoken")]] token : public contract {
    public:
        using contract::contract;

        static constexpr name STAKING_ACCOUNT { name("staking.ift") };

        /**
         * Allows `issuer` account to create a token in supply of `maximum_supply`. If validation is successful a new entry in statstable for token symbol scope gets created.
         *
         * @param issuer - the account that creates the token,
         * @param maximum_supply - the maximum supply set for the token created.
         *
         * @pre Token symbol has to be valid,
         * @pre Token symbol must not be already created,
         * @pre maximum_supply has to be smaller than the maximum supply allowed by the system: 1^62 - 1.
         * @pre Maximum supply must be positive;
         */
        [[eosio::action]]
        void create(const name&   issuer, const asset&  maximum_supply);
        /**
         *  This action issues to `to` account a `quantity` of tokens.
         *
         * @param to - the account to issue tokens to, it must be the same as the issuer,
         * @param quntity - the amount of tokens to be issued,
         * @memo - the memo string that accompanies the token issue transaction.
         */
        [[eosio::action]]
        void issue(const name& to, const asset& quantity, const string& memo);

        /**
         * The opposite for create action, if all validations succeed,
         * it debits the statstable.supply amount.
         *
         * @param quantity - the quantity of tokens to retire,
         * @param memo - the memo string to accompany the transaction.
         */
        [[eosio::action]]
        void retire(const asset& quantity, const string& memo);

        /**
         * Allows `from` account to transfer to `to` account the `quantity` tokens.
         * One account is debited and the other is credited with quantity tokens.
         *
         * @param from - the account to transfer from,
         * @param to - the account to be transferred to,
         * @param quantity - the quantity of tokens to be transferred,
         * @param memo - the memo string to accompany the transaction.
         */
        [[eosio::action]]
        void transfer(const name&    from,  const name&    to, const asset&   quantity, const string&  memo);
        /**
         * Allows `ram_payer` to create an account `owner` with zero balance for
         * token `symbol` at the expense of `ram_payer`.
         *
         * @param owner - the account to be created,
         * @param symbol - the token to be payed with by `ram_payer`,
         * @param ram_payer - the account that supports the cost of this action.
         *
         * More information can be read [here](https://github.com/EOSIO/eosio.contracts/issues/62)
         * and [here](https://github.com/EOSIO/eosio.contracts/issues/61).
         */
        [[eosio::action]]
        void open(const name& owner, const symbol& symbol, const name& ram_payer);

        /**
         * This action is the opposite for open, it closes the account `owner`
         * for token `symbol`.
         *
         * @param owner - the owner account to execute the close action for,
         * @param symbol - the symbol of the token to execute the close action for.
         *
         * @pre The pair of owner plus symbol has to exist otherwise no action is executed,
         * @pre If the pair of owner plus symbol exists, the balance has to be zero.
         */
        [[eosio::action]]
        void close(const name& owner, const symbol& symbol );

        static asset get_supply(const name& token_contract_account, const symbol_code& sym_code) {
            stats statstable( token_contract_account, sym_code.raw() );
            const auto& st = statstable.get( sym_code.raw() );
            return st.supply;
        }

        static asset get_balance(const name& token_contract_account, const name& owner, const symbol_code& sym_code) {
            accounts accountstable( token_contract_account, owner.value );
            const auto& ac = accountstable.get( sym_code.raw() );
            return ac.balance;
        }

    private:
        struct [[eosio::table]] account {
            asset    balance;
            uint64_t primary_key()const { return balance.symbol.code().raw(); }
        };

        struct [[eosio::table]] currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
        };

        struct [[eosio::table]] st_lock {
            uint64_t lock_id;
            block_timestamp release_time;
            symbol_code sym;
            uint64_t amount;
            uint64_t get_sym() const { return sym.raw(); }
            uint64_t primary_key() const { return lock_id; }
        };
        

        typedef eosio::multi_index< "accounts"_n, account > accounts;
        typedef eosio::multi_index< "stat"_n, currency_stats > stats;
        typedef eosio::multi_index<"locks"_n, st_lock, indexed_by<"bysym"_n, const_mem_fun<st_lock, uint64_t, &st_lock::get_sym>>> locks_mi;

        void sub_balance(const name& owner, const asset& value, bool is_check);
        void add_balance(const name& owner, const asset& value, const name& ram_payer, bool add_lock);

        bool check_lock(const name& owner, const asset& balance);

};
