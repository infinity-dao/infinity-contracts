#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/singleton.hpp>

using namespace eosio;
using std::string;

#define TOKEN_CONTRACT name("token.ift")
#define TOKEN_SYMBOL symbol("IFT", 8)
#define ADMIN_ACCOUNT name("admin.ift")
#define TOKEN_ISSUER name("issuer.ift")

struct currency_stats {
    asset    supply;
    asset    max_supply;
    name     issuer;
    uint64_t primary_key() const { return supply.symbol.code().raw(); }
};
typedef eosio::multi_index< "stat"_n, currency_stats > stats;

inline asset get_supply(name account, symbol_code sc) {
    stats stats_table(account, sc.raw());
    auto itr = stats_table.require_find(sc.raw(), "symbol not found");
    return itr->supply;
}

CONTRACT staking : public contract {
    public:
        staking(name receiver, name code, datastream<const char *> ds): contract(receiver, code, ds),
                _epochs(_self, _self.value),
                _symbols(_self, _self.value) {
            if (_epochs.exists()) {
                _epoch = _epochs.get();
            } else {
                _epoch = { 28800, 0, 0, asset(0, TOKEN_SYMBOL) };
            }
        }
        
        ACTION init(uint64_t number, uint64_t length, uint64_t start_time);
        ACTION distribute();
        ACTION addsymbol(symbol sym, name sname, uint64_t rate, uint64_t lock_time);
        ACTION removesymbol(symbol_code sc);
        ACTION updaterate(symbol_code sc, uint64_t rate);

        ACTION stake(name from, asset quantity, symbol_code sc);
        ACTION unstake(name from, asset quantity, name code, symbol sym);

        [[eosio::on_notify("*::transfer")]]
        void ontransfer(name from, name to, asset quantity, string memo);


    private:
        TABLE epoch {
            uint64_t length;
            uint64_t number;
            uint64_t end_time;
            asset distribute;
        };
        TABLE st_symbol {
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
        typedef singleton<"epoch"_n, epoch> epoch_sig;
        
        
        symbols_mi _symbols;
        epoch_sig _epochs;
        epoch _epoch;

        void _stake(name from, asset quantity, symbol_code sc);
        void _unstake(name from, asset quantity, name code, symbol sym);

        uint64_t _distribute(symbols_mi::const_iterator sym_itr, uint64_t ift_supply);
};