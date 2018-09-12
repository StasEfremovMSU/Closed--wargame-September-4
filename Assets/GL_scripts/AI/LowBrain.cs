using System.Collections;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// создать объект такого класса в каждом полку
/// </summary>
public class LowBrain  {

    // Переменные без true - приказы
    //  Переменные с  true - просчитанные, как оптимальные 

    /// <summary>
    ///  0 - просто стоять, 1 - не уходить, 2 - стоять насмерть
    /// </summary>
    public int stand;

    public weigt trueStand;

    /// <summary>
    /// 0 -  не стрелять, 1 - сохранять стрелы любой ценой, 2 - стрелять по ближайшему, 3 стрелять по ближайшей цели
    /// </summary>
    public int shooting;

    public weigt trueSshooting;

    /// <summary>
    /// 0 - не атаковать, 1 - только обороняться,  2 - агрессивное поведение
    /// </summary>
    public int atack;

    public weigt trueAtack;

    /// <summary>
    /// 0 - держаться линии фронта 1 - не двигаться 2 - не важно
    /// </summary>
    public int rotate;

    public weigt trueRotate;

    /// <summary>
    /// 0 - нет причин перестраиваться, 1 - заполнение рядов 2- приказ на новое построение
    /// </summary>
    public int rebuilding;

    public weigt trueRebuilding;

    /// <summary>
    /// 0 - не двигаться, 1 - идти к цели, 2 идти любой ценой
    /// </summary>
    public int moving;

    public weigt trueMoving;

    public void makeDecision()
    {
        float sum = 0;
        // Четыре базовых решения
        // 0 - стоять на месте
        // 1 - атаковать
        // 2 - идти
        // 3 - перестроиться
        // Дополнительные:
        //  - стрелять
        //  - поворачивать
        float[] var = new float[3];
 
        switch (stand)
        {
            case (0):
                /// вероятность, что будет стоять
                /// 10 - коэффициент стоять спокойно
                var[0] += 10;
                break;
            case(1):
                var[0] += 20;
                break;
            case (2):
                var[0] += 80;
                break;
        }

         switch (atack)
         {
            case (0):
                 var[1] += 5;
                break;
            case (1):
                var[1] += 20;
                break;
            case (2):
                var[1] += 80;
                break;
         }

         switch (moving)
         {
             case (0):
                 var[2] += 5;
                 break;
             case (1):
                 var[2] += 20;
                 break;
             case (2):
                 var[2] += 80;
                 break;
         }

         switch (rebuilding)
         {
             case (0):
                 var[3] += 5;
                 break;
             case (1):
                 var[3] += 20;
                 break;
             case (2):
                 var[3] += 80;
                 break;
         }

        // Прибавили мнение командира
         var[0] += trueStand.weight;
         var[1] += trueAtack.weight;
         var[2] += trueMoving.weight;
         var[3] += trueRebuilding.weight;
        /*
         switch (trueStand)
         {
             case (0):
                 /// вероятность, что будет стоять
                 /// 10 - коэффициент стоять спокойно
                 var[0] += 10;
                 break;
             case (1):
                 var[0] += 20;
                 break;
             case (2):
                 var[0] += 80;
                 break;
         }

         switch (trueAtack)
         {
             case (0):
                 var[1] += 5;
                 break;
             case (1):
                 var[1] += 20;
                 break;
             case (2):
                 var[1] += 80;
                 break;
         }

         switch (trueMoving)
         {
             case (0):
                 var[2] += 5;
                 break;
             case (1):
                 var[2] += 20;
                 break;
             case (2):
                 var[2] += 80;
                 break;
         }

         switch (trueRebuilding)
         {
             case (0):
                 var[3] += 5;
                 break;
             case (1):
                 var[3] += 20;
                 break;
             case (2):
                 var[3] += 80;
                 break;
         }
        */
         int desision = 99999;

         sum = var[0] + var[1] + var[2] + var[3];
         var[0] = var[0] / sum;
         var[1] = var[1] / sum;
         var[2] = var[2] / sum;
         var[3] = var[3] / sum;
         float r = Random.Range(0f, 1f);
         if (r < var[0])
         {
             desision = 0;
         }
         else
         {
             if (r < var[1])
             {
                 desision = 1;
             }
             else
             {
                 if (r < var[2])
                 {
                     desision = 2;
                 }
                 else
                 {
                     if (r < var[3])
                     {
                         desision = 3;
                     }
                     else
                     {
                         desision = 4;
                     }
                 }
             }
         }

         int AdditDes = 99999;

         // добавим стрельбу и повороты
         switch (desision)
         {
             case (0):
                 /// Случай, если полк стоит, то стреляет, или поворачивает?
                 float[] var2 = new float[2];
                 //  0 - стреляет
                 // 1 - поворачивает
                 switch (shooting)
                 {
                     case (0):
                         var2[0] += 5;
                         break;
                     case (1):
                         var2[0] += 20;
                         break;
                     case (2):
                         var2[0] += 80;
                         break;
                 }
                 /*switch (trueSshooting)
                 {
                     case (0):
                         var2[0] += 5;
                         break;
                     case (1):
                         var2[0] += 20;
                         break;
                     case (2):
                         var2[0] += 80;
                         break;
                 }*/
                 switch (rotate)
                 {
                     case (0):
                         var2[1] += 5;
                         break;
                     case (1):
                         var2[1] += 20;
                         break;
                     case (2):
                         var2[1] += 80;
                         break;
                 }
                /* switch (trueRotate)
                 {
                     case (0):
                         var[1] += 5;
                         break;
                     case (1):
                         var[1] += 20;
                         break;
                     case (2):
                         var[1] += 80;
                         break;
                 }
                 */
                 sum = 0;

                 sum = var[0] + var[1];

                 var[0] = var[0] / sum;
                 var[1] = var[1] / sum;

                 r = Random.Range(0f, 1f);
                 if (r < var[0])
                 {
                     AdditDes = 0;
                 }
                 else
                 {
                     AdditDes = 1;
                 }


                 break;
             case (3):
                 /// Случай, если полк перестраивается, будет ли поворачивать?
                 
                 /// Пока всегда  поворачивать при перестроении 
                 AdditDes = 1;

                 break;



         }


        /// Теперь сводим к приказам
         switch (desision)
         {
             case (0):
                 holdPosition();
                 break;

             case (1):
                 makeAttack();
                 break;

             case(2):
                 fmoving();
                 break;

             case (3):
                 frebuilding();
                 break;

         }
    }

    void holdPosition()
    {

    }

    void makeRotation()
    {

    }

    void makeAttack()
    {

    }

    void shoot()
    {

    }

    void fmoving()
    {

    }

    void frebuilding()
    {

    }

    public void reaction()
    {

    }



	
}



public struct weigt
{
    public float weight;
    public int des;
}