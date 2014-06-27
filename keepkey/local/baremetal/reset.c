/*
 * This file is part of the TREZOR project.
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <crypto.h>
#include <interface.h>
#include <keepkey_board.h>

#include "reset.h"
#include "storage.h"
#include "messages.h"
#include "fsm.h"
#include "protect.h"
#include "util.h"

static uint32_t strength;
static uint8_t  int_entropy[32];
static bool     awaiting_entropy = false;

void reset_init(bool display_random, uint32_t _strength, bool passphrase_protection, bool pin_protection, const char *language, const char *label)
{
	if (_strength != 128 && _strength != 192 && _strength != 256) {
		fsm_sendFailure(FailureType_Failure_SyntaxError, "Invalid strength (has to be 128, 192 or 256 bits)");
		layout_home();
		return;
	}

	strength = _strength;

	random_buffer(int_entropy, 32);

	char ent_str[4][17];
	data2hex(int_entropy     , 8, ent_str[0]);
	data2hex(int_entropy +  8, 8, ent_str[1]);
	data2hex(int_entropy + 16, 8, ent_str[2]);
	data2hex(int_entropy + 24, 8, ent_str[3]);

	if (display_random) {
                if(!confirm("Internal entropy: %s %s %s %s", ent_str[0], ent_str[1], ent_str[2], ent_str[3])) {
			fsm_sendFailure(FailureType_Failure_ActionCancelled, "Reset cancelled");
			layout_home();
			return;
		}
	}

	storage.has_passphrase_protection = true;
	storage.passphrase_protection = passphrase_protection;
	storage_setLanguage(language);
	storage_setLabel(label);

	EntropyRequest resp;
	memset(&resp, 0, sizeof(EntropyRequest));
	msg_write(MessageType_MessageType_EntropyRequest, &resp);
	awaiting_entropy = true;
}

static char current_word[10];

void reset_entropy(const uint8_t *ext_entropy, uint32_t len)
{
	if (!awaiting_entropy) {
		fsm_sendFailure(FailureType_Failure_UnexpectedMessage, "Not in Reset mode");
		return;
	}
	SHA256_CTX ctx;
	sha256_Init(&ctx);
	sha256_Update(&ctx, int_entropy, 32);
	sha256_Update(&ctx, ext_entropy, len);
	sha256_Final(int_entropy, &ctx);
	strlcpy(storage.mnemonic, mnemonic_from_data(int_entropy, strength / 8), sizeof(storage.mnemonic));
	memset(int_entropy, 0, 32);
	awaiting_entropy = false;

	int pass, word_pos, i = 0, j;

	for (pass = 0; pass < 2; pass++) {
		i = 0;
		for (word_pos = 1; word_pos <= (int)strength/32*3; word_pos++) {
			// copy current_word
			j = 0;
			while (storage.mnemonic[i] != ' ' && storage.mnemonic[i] != 0 && j + 1 < (int)sizeof(current_word)) {
				current_word[j] = storage.mnemonic[i];
				i++; j++;
			}
			current_word[j] = 0; if (storage.mnemonic[i] != 0) i++;
			char desc[] = "##th word";
			if (word_pos < 10) {
				desc[0] = ' ';
			} else {
				desc[0] = '0' + word_pos / 10;
			}
			desc[1] = '0' + word_pos % 10;
			if (word_pos == 1 || word_pos == 21) {
				desc[2] = 's'; desc[3] = 't';
			} else
			if (word_pos == 2 || word_pos == 22) {
				desc[2] = 'n'; desc[3] = 'd';
			} else
			if (word_pos == 3 || word_pos == 23) {
				desc[2] = 'r'; desc[3] = 'd';
			}
			if (word_pos == (int)strength/32*3) { // last word
				if (pass == 1) {
                                    confirm("Finish: Please check the seed: %s %s",  
                                            (word_pos < 10 ? desc + 1 : desc), current_word);
				} else {
				    confirm("Again: Write down the seed %s %s", 
                                            (word_pos < 10 ? desc + 1 : desc), current_word);
				}
			} else {
				if (pass == 1) {
                                        confirm("Next: lease check the seed %s %s", 
                                                (word_pos < 10 ? desc + 1 : desc), current_word);
				} else {
                                        confirm("Next: Please check the seed %s %s", 
                                                (word_pos < 10 ? desc + 1 : desc), current_word);
				}
			}
                        if(!confirm("Confirm mnemonic")) {
				storage_reset();
				layout_home();
				return;
			}
		}
	}

	storage.has_mnemonic = true;
	storage_commit();
	fsm_sendSuccess("Device reset");
	layout_home();
}

uint32_t reset_get_int_entropy(uint8_t *entropy) {
	memcpy(entropy, int_entropy, 32);
	return 32;
}

const char *reset_get_word(void) {
	return current_word;
}
