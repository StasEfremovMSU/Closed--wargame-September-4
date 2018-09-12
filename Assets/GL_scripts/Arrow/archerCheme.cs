using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class archerCheme {
    // Класс со схемами стрельбы, который мы в дальнейшем будем сериализовать

    //Коэффициент, числитель - точность стрельбы в лоб, знаменатель - максимальная дальность
    static private float Koeff1 = (- 18/ 100);
    static private float Koeff2 = 18;

    static private float Koeff3 = (-12 / 200);
    static private float Koeff4 = 12;

    // ArcherSkill должен быть от одного до нуля

    /// <summary>
    /// Функция, которая получает данные о солдате и расстоянии и говорит успешен ли выстрел
    /// </summary>
    public static bool makeBum(bool Fline, int typeArcher, float archerSkill, float dist, bool directView)
    {
        Vector3 res = new Vector3(0, 0, 0);
        float atTest = 0;

        switch (typeArcher)
        {
                //  Тестовая функция, 
            case (0):
                
                // Если стрелок находится в первом ряду и есть прямая видимость, то стреляет прямо
                if (Fline == true && directView == true)
                {
                    atTest = Koeff1 * dist * archerSkill + Koeff2; 

                }
                // во всех остальных случаях стрельба идет навесом
                else
                {
                    int x = 0;
                    if (directView == true)
                        x = 1;

                    atTest = (Koeff3 * dist * archerSkill)* ( 1 + 0.2f * x) + Koeff4;

                }
            break;
        }

        /// На данный момент кода мы определили тест на попадание

        if (Random.Range(0, 20) < atTest)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

}
