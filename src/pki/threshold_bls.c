#include <pbc/pbc.h>
#include <pbc/pbc_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PARTICIPANTS 10
#define SECURITY_PARAM 160

typedef struct {
    pairing_ptr pairing;
    element_t g1, g2;
    element_t *sk_shares;          // 私钥份额 sk_i
    element_t *pk_shares_1;        // 公钥份额 pk_i,1 ∈ G1
    element_t *pk_shares_2;        // 公钥份额 pk_i,2 ∈ G2  
    element_t apk_1, apk_2;        // 聚合公钥
    element_t msk;                 // 主私钥 (用于验证)
    int n;                         // 参与方数量
    int t;                         // 门限值
} ThresholdBLS;

typedef struct {
    element_t sigma;               // 部分签名 σ_i ∈ G1
    element_t R1, R2;              // DLEQ证明组件
    element_t z;                   // DLEQ证明响应
    int participant_id;
} PartialSignature;

// 打印分隔线
void print_separator(const char* pattern, int length) {
    for (int i = 0; i < length; i++) {
        printf("%s", pattern);
    }
    printf("\n");
}

// 哈希函数模拟
void hash_to_G1(element_t out, const char *msg, pairing_t pairing) {
    element_from_hash(out, (void*)msg, strlen(msg));
}

void hash_to_Zr(element_t out, const char *msg, pairing_t pairing) {
    element_t temp;
    element_init_Zr(temp, pairing);
    element_from_hash(temp, (void*)msg, strlen(msg));
    element_set(out, temp);
    element_clear(temp);
}

// 初始化阈值BLS系统
void threshold_bls_init(ThresholdBLS *tbls, int n, int t, pairing_t pairing) {
    tbls->n = n;
    tbls->t = t;
    tbls->pairing = pairing;
    
    // 初始化生成元
    element_init_G1(tbls->g1, pairing);
    element_init_G2(tbls->g2, pairing);
    element_random(tbls->g1);
    element_random(tbls->g2);
    
    // 初始化密钥份额数组
    tbls->sk_shares = (element_t*)malloc(n * sizeof(element_t));
    tbls->pk_shares_1 = (element_t*)malloc(n * sizeof(element_t));
    tbls->pk_shares_2 = (element_t*)malloc(n * sizeof(element_t));
    
    for (int i = 0; i < n; i++) {
        element_init_Zr(tbls->sk_shares[i], pairing);
        element_init_G1(tbls->pk_shares_1[i], pairing);
        element_init_G2(tbls->pk_shares_2[i], pairing);
    }
    
    // 初始化聚合公钥和主私钥
    element_init_G1(tbls->apk_1, pairing);
    element_init_G2(tbls->apk_2, pairing);
    element_init_Zr(tbls->msk, pairing);
    
    printf("Threshold BLS system initialized with n=%d, t=%d\n", n, t);
}

// 清理资源
void threshold_bls_clear(ThresholdBLS *tbls) {
    for (int i = 0; i < tbls->n; i++) {
        element_clear(tbls->sk_shares[i]);
        element_clear(tbls->pk_shares_1[i]);
        element_clear(tbls->pk_shares_2[i]);
    }
    free(tbls->sk_shares);
    free(tbls->pk_shares_1);
    free(tbls->pk_shares_2);
    
    element_clear(tbls->g1);
    element_clear(tbls->g2);
    element_clear(tbls->apk_1);
    element_clear(tbls->apk_2);
    element_clear(tbls->msk);
}

// 模拟DKG过程生成密钥份额
void simulate_dkg(ThresholdBLS *tbls) {
    printf("\n=== Simulating Distributed Key Generation ===\n");
    
    // 模拟Shamir秘密共享
    element_t coefficients[tbls->t];
    for (int i = 0; i < tbls->t; i++) {
        element_init_Zr(coefficients[i], tbls->pairing);
        element_random(coefficients[i]);
    }
    
    // 计算主私钥 (常数项)
    element_set(tbls->msk, coefficients[0]);
    
    // 为每个参与方计算份额
    for (int i = 0; i < tbls->n; i++) {
        int participant_id = i + 1;  // ID从1开始
        
        // 计算多项式在点participant_id的值
        element_t share, term, power, exponent;
        element_init_Zr(share, tbls->pairing);
        element_init_Zr(term, tbls->pairing);
        element_init_Zr(power, tbls->pairing);
        element_init_Zr(exponent, tbls->pairing);
        element_set0(share);
        
        for (int j = 0; j < tbls->t; j++) {
            // term = coefficient[j] * (participant_id)^j
            element_set_si(power, participant_id);
            element_set_si(exponent, j);
            element_pow_zn(term, power, exponent);  // term = participant_id^j
            element_mul(term, term, coefficients[j]);
            element_add(share, share, term);
        }
        
        element_set(tbls->sk_shares[i], share);
        
        // 计算公钥份额
        element_mul_zn(tbls->pk_shares_1[i], tbls->g1, tbls->sk_shares[i]);
        element_mul_zn(tbls->pk_shares_2[i], tbls->g2, tbls->sk_shares[i]);
        
        element_clear(share);
        element_clear(term);
        element_clear(power);
        element_clear(exponent);
        
        printf("Participant %d: sk_share = ", participant_id);
        element_printf("%B\n", tbls->sk_shares[i]);
    }
    
    // 清理系数
    for (int i = 0; i < tbls->t; i++) {
        element_clear(coefficients[i]);
    }
}

// 聚合公钥
void aggregate_keys(ThresholdBLS *tbls) {
    printf("\n=== Aggregating Public Keys ===\n");
    
    element_set0(tbls->apk_1);
    element_set0(tbls->apk_2);
    
    for (int i = 0; i < tbls->n; i++) {
        element_add(tbls->apk_1, tbls->apk_1, tbls->pk_shares_1[i]);
        element_add(tbls->apk_2, tbls->apk_2, tbls->pk_shares_2[i]);
    }
    
    printf("Aggregated public key computed:\n");
    element_printf("apk_1 = %B\n", tbls->apk_1);
    element_printf("apk_2 = %B\n", tbls->apk_2);
}

// 计算Lagrange系数
void compute_lagrange_coeff(element_t *coeffs, int *participants, int k, pairing_t pairing) {
    for (int i = 0; i < k; i++) {
        element_t numerator, denominator, term, zero, diff;
        element_init_Zr(numerator, pairing);
        element_init_Zr(denominator, pairing);
        element_init_Zr(term, pairing);
        element_init_Zr(zero, pairing);
        element_init_Zr(diff, pairing);
        
        element_set1(numerator);
        element_set1(denominator);
        element_set0(zero);
        
        for (int j = 0; j < k; j++) {
            if (i != j) {
                // numerator *= (0 - participant_id_j)
                element_set_si(term, participants[j]);
                element_sub(diff, zero, term);  // diff = -participant_id_j
                element_mul(numerator, numerator, diff);
                
                // denominator *= (participant_id_i - participant_id_j)
                element_set_si(term, participants[i] - participants[j]);
                element_mul(denominator, denominator, term);
            }
        }
        
        element_div(coeffs[i], numerator, denominator);
        
        element_clear(numerator);
        element_clear(denominator);
        element_clear(term);
        element_clear(zero);
        element_clear(diff);
    }
}

// 生成部分签名和DLEQ证明
void partial_sign(ThresholdBLS *tbls, PartialSignature *psig, int participant_id, 
                  const char *message, double *time_spent) {
    clock_t start = clock();
    
    printf("\n=== Participant %d Generating Partial Signature ===\n", participant_id);
    
    psig->participant_id = participant_id;
    
    // 初始化元素
    element_init_G1(psig->sigma, tbls->pairing);
    element_init_G1(psig->R1, tbls->pairing);
    element_init_G1(psig->R2, tbls->pairing);
    element_init_Zr(psig->z, tbls->pairing);
    
    // 获取参与方的私钥份额
    element_t sk_share;
    element_init_Zr(sk_share, tbls->pairing);
    element_set(sk_share, tbls->sk_shares[participant_id-1]);
    
    // 1. 生成部分签名 σ_i = sk_i * H(msg)
    element_t H_msg;
    element_init_G1(H_msg, tbls->pairing);
    hash_to_G1(H_msg, message, tbls->pairing);
    
    element_mul_zn(psig->sigma, H_msg, sk_share);
    
    // 2. 生成DLEQ证明 π = ProveDLEQ(g1, msg, pk_i,1, σ_i, sk_i)
    element_t r, c;
    element_init_Zr(r, tbls->pairing);
    element_init_Zr(c, tbls->pairing);
    
    // 选择随机数 r
    element_random(r);
    
    // 计算 R1 = r * g1, R2 = r * H(msg)
    element_mul_zn(psig->R1, tbls->g1, r);
    element_mul_zn(psig->R2, H_msg, r);
    
    // 计算挑战 c = H(g1, H(msg), pk_i,1, σ_i, R1, R2)
    // 简化处理：使用字符串拼接作为哈希输入
    char challenge_data[1024];
    unsigned char r1_bytes[1024], r2_bytes[1024];
    int r1_len = element_to_bytes(r1_bytes, psig->R1);
    int r2_len = element_to_bytes(r2_bytes, psig->R2);
    
    snprintf(challenge_data, sizeof(challenge_data), "g1|%s|participant%d|R1_len%d|R2_len%d", 
             message, participant_id, r1_len, r2_len);
    hash_to_Zr(c, challenge_data, tbls->pairing);
    
    // 计算响应 z = r + c * sk_i
    element_mul(psig->z, c, sk_share);
    element_add(psig->z, psig->z, r);
    
    printf("Partial signature generated:\n");
    element_printf("σ_%d = %B\n", participant_id, psig->sigma);
    
    // 清理临时变量
    element_clear(sk_share);
    element_clear(H_msg);
    element_clear(r);
    element_clear(c);
    
    *time_spent = ((double)(clock() - start)) / CLOCKS_PER_SEC * 1000.0;
}

// 验证DLEQ证明
int verify_dleq_proof(ThresholdBLS *tbls, PartialSignature *psig, const char *message) {
    printf("\n=== Verifying DLEQ Proof for Participant %d ===\n", psig->participant_id);
    
    element_t c, left1, right1, left2, right2, temp;
    element_init_Zr(c, tbls->pairing);
    element_init_G1(left1, tbls->pairing);
    element_init_G1(right1, tbls->pairing);
    element_init_G1(left2, tbls->pairing);
    element_init_G1(right2, tbls->pairing);
    element_init_G1(temp, tbls->pairing);
    
    // 重新计算挑战 c
    char challenge_data[1024];
    unsigned char r1_bytes[1024], r2_bytes[1024];
    int r1_len = element_to_bytes(r1_bytes, psig->R1);
    int r2_len = element_to_bytes(r2_bytes, psig->R2);
    
    snprintf(challenge_data, sizeof(challenge_data), "g1|%s|participant%d|R1_len%d|R2_len%d", 
             message, psig->participant_id, r1_len, r2_len);
    hash_to_Zr(c, challenge_data, tbls->pairing);
    
    // 验证第一个方程: z * g1 == R1 + c * pk_i,1
    element_mul_zn(left1, tbls->g1, psig->z);
    
    element_mul_zn(temp, tbls->pk_shares_1[psig->participant_id-1], c);
    element_add(right1, psig->R1, temp);
    
    // 验证第二个方程: z * H(msg) == R2 + c * σ_i
    element_t H_msg;
    element_init_G1(H_msg, tbls->pairing);
    hash_to_G1(H_msg, message, tbls->pairing);
    
    element_mul_zn(left2, H_msg, psig->z);
    
    element_mul_zn(temp, psig->sigma, c);
    element_add(right2, psig->R2, temp);
    
    int valid1 = !element_cmp(left1, right1);
    int valid2 = !element_cmp(left2, right2);
    
    printf("DLEQ Proof Verification:\n");
    printf("Equation 1 (z*g1 == R1 + c*pk1): %s\n", valid1 ? "PASS" : "FAIL");
    printf("Equation 2 (z*H(msg) == R2 + c*sigma): %s\n", valid2 ? "PASS" : "FAIL");
    
    element_clear(c);
    element_clear(left1);
    element_clear(right1);
    element_clear(left2);
    element_clear(right2);
    element_clear(temp);
    element_clear(H_msg);
    
    return valid1 && valid2;
}

// 组合签名
void combine_signatures(ThresholdBLS *tbls, element_t final_sigma, 
                       PartialSignature **psigs, int k, const char *message,
                       double *time_spent) {
    clock_t start = clock();
    
    printf("\n=== Combining %d Partial Signatures ===\n", k);
    
    element_init_G1(final_sigma, tbls->pairing);
    element_set0(final_sigma);
    
    // 提取参与方ID
    int participants[k];
    for (int i = 0; i < k; i++) {
        participants[i] = psigs[i]->participant_id;
    }
    
    // 计算Lagrange系数
    element_t lagrange_coeffs[k];
    for (int i = 0; i < k; i++) {
        element_init_Zr(lagrange_coeffs[i], tbls->pairing);
    }
    compute_lagrange_coeff(lagrange_coeffs, participants, k, tbls->pairing);
    
    // 应用Lagrange插值: σ = Σ L_i * σ_i
    element_t temp;
    element_init_G1(temp, tbls->pairing);
    
    for (int i = 0; i < k; i++) {
        element_mul_zn(temp, psigs[i]->sigma, lagrange_coeffs[i]);
        element_add(final_sigma, final_sigma, temp);
        
        printf("L_%d = ", participants[i]);
        element_printf("%B\n", lagrange_coeffs[i]);
    }
    
    printf("Final combined signature:\n");
    element_printf("σ = %B\n", final_sigma);
    
    // 清理
    element_clear(temp);
    for (int i = 0; i < k; i++) {
        element_clear(lagrange_coeffs[i]);
    }
    
    *time_spent = ((double)(clock() - start)) / CLOCKS_PER_SEC * 1000.0;
}

// 验证最终签名
int verify_signature(ThresholdBLS *tbls, element_t sigma, const char *message, double *time_spent) {
    clock_t start = clock();
    
    printf("\n=== Verifying Final Signature ===\n");
    
    element_t t_val, left1, left2, temp1, temp2;
    element_t H_msg;
    element_t pair_left, pair_right;
    
    element_init_Zr(t_val, tbls->pairing);
    element_init_G1(left1, tbls->pairing);
    element_init_G1(left2, tbls->pairing);
    element_init_G1(temp1, tbls->pairing);
    element_init_G1(temp2, tbls->pairing);
    element_init_G1(H_msg, tbls->pairing);
    element_init_GT(pair_left, tbls->pairing);
    element_init_GT(pair_right, tbls->pairing);
    
    // 随机选择 t
    element_random(t_val);
    
    // 计算 H_msg
    hash_to_G1(H_msg, message, tbls->pairing);
    
    // 计算左边: σ + t * apk_1
    element_mul_zn(temp1, tbls->apk_1, t_val);  // t * apk_1
    element_add(left1, sigma, temp1);           // σ + t * apk_1
    
    // 计算右边基准: H_msg + t * g1
    element_mul_zn(temp2, tbls->g1, t_val);     // t * g1
    element_add(left2, H_msg, temp2);           // H_msg + t * g1
    
    // 计算配对: e(σ + t·apk_1, g_2)
    pairing_apply(pair_left, left1, tbls->g2, tbls->pairing);
    
    // 计算配对: e(H_msg + t·g_1, apk_2)
    pairing_apply(pair_right, left2, tbls->apk_2, tbls->pairing);
    
    int valid = !element_cmp(pair_left, pair_right);
    
    printf("Signature Verification: %s\n", valid ? "VALID" : "INVALID");
    element_printf("t = %B\n", t_val);
    
    // 清理
    element_clear(t_val);
    element_clear(left1);
    element_clear(left2);
    element_clear(temp1);
    element_clear(temp2);
    element_clear(H_msg);
    element_clear(pair_left);
    element_clear(pair_right);
    
    *time_spent = ((double)(clock() - start)) / CLOCKS_PER_SEC * 1000.0;
    
    return valid;
}

// 性能测试
void performance_test(ThresholdBLS *tbls, const char *message) {
    printf("\n");
    print_separator("=", 60);
    printf("PERFORMANCE TEST\n");
    print_separator("=", 60);
    
    double total_time = 0.0;
    double times[4] = {0}; // 存储各阶段时间
    
    // 测试部分签名生成
    printf("1. Partial Signature Generation:\n");
    PartialSignature psigs[tbls->t];
    for (int i = 0; i < tbls->t; i++) {
        double time_used;
        partial_sign(tbls, &psigs[i], i+1, message, &time_used);
        times[0] += time_used;
        printf("   Participant %d: %.3f ms\n", i+1, time_used);
    }
    times[0] /= tbls->t; // 平均时间
    
    // 测试DLEQ证明验证
    printf("\n2. DLEQ Proof Verification:\n");
    clock_t start = clock();
    for (int i = 0; i < tbls->t; i++) {
        verify_dleq_proof(tbls, &psigs[i], message);
    }
    times[1] = ((double)(clock() - start)) / CLOCKS_PER_SEC * 1000.0;
    printf("   Total time: %.3f ms\n", times[1]);
    
    // 测试签名组合
    printf("\n3. Signature Combination:\n");
    PartialSignature *psig_ptrs[tbls->t];
    for (int i = 0; i < tbls->t; i++) {
        psig_ptrs[i] = &psigs[i];
    }
    
    element_t final_sigma;
    combine_signatures(tbls, final_sigma, psig_ptrs, tbls->t, message, &times[2]);
    
    // 测试签名验证
    printf("\n4. Final Signature Verification:\n");
    int valid = verify_signature(tbls, final_sigma, message, &times[3]);
    
    // 输出性能结果
    printf("\n");
    print_separator("-", 60);
    printf("PERFORMANCE SUMMARY\n");
    print_separator("-", 60);
    printf("Operation                    | Time (ms)\n");
    printf("-----------------------------|-----------\n");
    printf("Partial Sign (avg)           | %8.3f\n", times[0]);
    printf("DLEQ Verify (total)          | %8.3f\n", times[1]);
    printf("Signature Combination        | %8.3f\n", times[2]);
    printf("Final Verification           | %8.3f\n", times[3]);
    printf("-----------------------------|-----------\n");
    printf("Total                        | %8.3f\n", 
           times[0]*tbls->t + times[1] + times[2] + times[3]);
    printf("\nSignature Valid: %s\n", valid ? "YES" : "NO");
    
    // 清理
    element_clear(final_sigma);
    for (int i = 0; i < tbls->t; i++) {
        element_clear(psigs[i].sigma);
        element_clear(psigs[i].R1);
        element_clear(psigs[i].R2);
        element_clear(psigs[i].z);
    }
}

int main(int argc, char **argv) {
    // 初始化PBC配对
    pairing_t pairing;
    
    // 使用默认参数初始化配对
    char param[1024];
    size_t count = 0;
    
    // 生成配对参数
    if (argc > 1) {
        // 从文件读取配对参数
        FILE *fp = fopen(argv[1], "r");
        if (fp) {
            count = fread(param, 1, sizeof(param)-1, fp);
            fclose(fp);
        }
    }
    
    if (count == 0) {
        // 使用内置参数
        snprintf(param, sizeof(param), 
            "type a\n"
            "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
            "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
            "r 730750818665451621361119245571504901405976559617\n"
            "exp2 159\n"
            "exp1 107\n"
            "sign1 1\n"
            "sign0 1\n");
    }
    
    pairing_init_set_buf(pairing, param, strlen(param));
    
    if (!pairing_is_symmetric(pairing)) {
        printf("Warning: Using asymmetric pairing\n");
    }
    
    // 创建阈值BLS系统
    ThresholdBLS tbls;
    int n = 5;  // 总参与方数
    int t = 3;  // 门限值
    
    threshold_bls_init(&tbls, n, t, pairing);
    
    // 模拟DKG过程
    simulate_dkg(&tbls);
    
    // 聚合公钥
    aggregate_keys(&tbls);
    
    // 测试消息
    const char *test_message = "Hello Threshold BLS!";
    
    // 运行性能测试
    performance_test(&tbls, test_message);
    
    // 清理资源
    threshold_bls_clear(&tbls);
    pairing_clear(pairing);
    
    printf("\nProgram completed successfully!\n");
    return 0;
}