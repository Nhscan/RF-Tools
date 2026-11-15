"""Run the repository's actual Princeton feed function against synthetic remote frames."""
from pathlib import Path
import subprocess
import argparse
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware-root', type=Path, required=True)
root = parser.parse_args().firmware_root.resolve()
src = (root/'lib/subghz/protocols/princeton.c').read_text()
feed = src[src.index('void subghz_protocol_decoder_princeton_feed('):src.index('\nSubGhzProtocolStatus subghz_protocol_decoder_princeton_serialize')]
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#define furi_assert assert
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
#define PRINCETON_GUARD_TIME_DEFALUT 30
enum {PrincetonDecoderStepReset, PrincetonDecoderStepSaveDuration, PrincetonDecoderStepCheckDuration};
static const struct {uint32_t te_short,te_long,te_delta,min_count_bit_for_found;} subghz_protocol_princeton_const={390,1170,300,24};
typedef struct {uint32_t parser_step,te_last,decode_count_bit; uint64_t decode_data;} SubGhzBlockDecoder;
typedef struct Base {void (*callback)(struct Base*,void*); void* context;} Base;
typedef struct {Base base; SubGhzBlockDecoder decoder; struct {uint64_t data; uint32_t data_count_bit;} generic; uint32_t te,last_data,guard_time;} SubGhzProtocolDecoderPrinceton;
static void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder* d,int bit){d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;}
static uint32_t saved_te; static uint64_t saved_key; static unsigned callbacks;
static void snapshot(Base* b,void* ctx){(void)ctx;SubGhzProtocolDecoderPrinceton* d=(void*)b;saved_te=d->te;saved_key=d->generic.data;callbacks++;}
'''
suffix = r'''
static void frame(SubGhzProtocolDecoderPrinceton* d,uint32_t te,uint32_t key){
 for(int bit=23;bit>=0;bit--){bool one=(key>>bit)&1;subghz_protocol_decoder_princeton_feed(d,true,te*(one?3:1));subghz_protocol_decoder_princeton_feed(d,false,te*(one?1:3));}
 subghz_protocol_decoder_princeton_feed(d,true,te);
 subghz_protocol_decoder_princeton_feed(d,false,te*30);
}
int main(void){
 for(uint32_t te=350;te<=390;te+=10){
  SubGhzProtocolDecoderPrinceton d={0};d.base.callback=snapshot;callbacks=0;
  subghz_protocol_decoder_princeton_feed(&d,false,te*30);
  frame(&d,te,0xA5A5A5);frame(&d,te,0xA5A5A5);
  assert(callbacks==1 && saved_te==te && saved_key==0xA5A5A5);
  assert(d.te==0); // old deferred serialization reads a reset accumulator
  subghz_protocol_decoder_princeton_feed(&d,true,te);
  subghz_protocol_decoder_princeton_feed(&d,false,te*3);
  assert(d.te==te*4 && saved_te==te); // snapshot survives subsequent RX
 }
 puts("PASS: actual Princeton decoder, five TE values, stable snapshot after reset and further RX");
}
'''
with tempfile.TemporaryDirectory(prefix='rf-tools-test-') as tmp:
    directory = Path(tmp)
    source = directory / 'test_princeton_snapshot.c'
    executable = directory / 'test_princeton_snapshot.exe'
    source.write_text(prefix + feed + suffix)
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-lm', '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

